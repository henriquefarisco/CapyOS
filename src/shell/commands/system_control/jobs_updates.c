#include "internal/system_control_internal.h"

int cmd_job_run(struct shell_context *ctx, int argc, char **argv) {
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

static int update_runtime_bytes_writer(const char *path, const uint8_t *data,
                                       size_t len) {
  struct session_context *previous_session = session_active();
  struct file *file = NULL;
  struct dentry *d = NULL;
  int rc = -1;

  if (!path || (!data && len > 0u)) {
    return -1;
  }
  session_set_active(NULL);
  if (vfs_lookup(path, &d) == 0) {
    (void)vfs_unlink(path);
  }
  if (vfs_create(path, VFS_MODE_FILE, NULL) == 0 ||
      vfs_lookup(path, &d) == 0) {
    file = vfs_open(path, VFS_OPEN_WRITE);
    if (file) {
      rc = (len == 0u || vfs_write(file, data, len) == (long)len) ? 0 : -1;
      vfs_close(file);
    }
  }
  session_set_active(previous_session);
  return rc;
}

static int update_runtime_remover(const char *path) {
  struct session_context *previous_session = session_active();
  int rc = 0;

  session_set_active(NULL);
  rc = vfs_unlink(path);
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

static const char *update_gate_status(uint8_t ready) {
  return ready ? "ok" : "fail";
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

int cmd_update_check(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_update_fetch(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-fetch\nBaixa o manifesto remoto configurado, valida assinatura/hash/trilha e atualiza o catalogo local.\n",
        "Usage: update-fetch\nDownloads the configured remote manifest, validates signature/hash/track and updates the local catalog.\n",
        "Uso: update-fetch\nDescarga el manifiesto remoto configurado, valida firma/hash/ruta y actualiza el catalogo local.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-fetch");
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  update_agent_set_remover(update_runtime_remover);
  rc = refresh_update_agent_service_state(update_agent_fetch_remote_manifest(),
                                          &status);
  update_agent_set_writer(NULL);
  update_agent_set_remover(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "nao foi possivel buscar o manifesto remoto de update",
        "could not fetch the remote update manifest",
        "no fue posible descargar el manifiesto remoto de update"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "manifesto remoto aceito no catalogo local",
                                     "remote manifest accepted into the local catalog",
                                     "manifiesto remoto aceptado en el catalogo local"));
  shell_print("available=");
  shell_print(status.available_version[0] ? status.available_version : "-");
  shell_print(" remote=");
  shell_print(status.remote_manifest_url[0] ? status.remote_manifest_url : "-");
  shell_print(" payload=");
  shell_print(status.payload_url[0] ? status.payload_url : "-");
  shell_newline();
  update_history_append_event("fetch", &status);
  return 0;
}

int cmd_update_download_payload(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-download-payload\nBaixa o payload declarado pelo manifesto cacheado, valida SHA-256 e grava em /system/update/payload.bin.\n",
        "Usage: update-download-payload\nDownloads the payload declared by the cached manifest, validates SHA-256 and writes /system/update/payload.bin.\n",
        "Uso: update-download-payload\nDescarga el payload declarado por el manifiesto cacheado, valida SHA-256 y graba en /system/update/payload.bin.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-download-payload");
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  update_agent_set_bytes_writer(update_runtime_bytes_writer);
  rc = refresh_update_agent_service_state(update_agent_download_payload(),
                                          &status);
  update_agent_set_bytes_writer(NULL);
  update_agent_set_writer(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "nao foi possivel baixar e verificar o payload de update",
        "could not download and verify the update payload",
        "no fue posible descargar y verificar el payload de update"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "payload de update baixado e verificado",
                                     "update payload downloaded and verified",
                                     "payload de update descargado y verificado"));
  shell_print("cache=");
  shell_print(status.payload_cache_path[0] ? status.payload_cache_path : "-");
  shell_print(" sha256=");
  shell_print(status.payload_cache_sha256[0] ? status.payload_cache_sha256 : "-");
  shell_print(" source=");
  shell_print(status.payload_url[0] ? status.payload_url : "-");
  shell_newline();
  update_history_append_event("download-payload", &status);
  return 0;
}

int cmd_update_prepare(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-prepare\nBusca manifesto remoto, baixa/verifica payload, prepara staging e arma a ativacao sem aplicar o boot slot.\n",
        "Usage: update-prepare\nFetches the remote manifest, downloads/verifies payload, prepares staging and arms activation without applying the boot slot.\n",
        "Uso: update-prepare\nDescarga el manifiesto remoto, descarga/verifica el payload, prepara staging y arma la activacion sin aplicar el boot slot.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-prepare");
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  update_agent_set_bytes_writer(update_runtime_bytes_writer);
  update_agent_set_remover(update_runtime_remover);
  rc = refresh_update_agent_service_state(update_agent_prepare_staged_update(),
                                          &status);
  update_agent_set_remover(NULL);
  update_agent_set_bytes_writer(NULL);
  update_agent_set_writer(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "nao foi possivel preparar e armar o update verificado",
        "could not prepare and arm the verified update",
        "no fue posible preparar y armar el update verificado"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(
      language,
      "update verificado preparado e armado para ativacao",
      "verified update prepared and armed for activation",
      "update verificado preparado y armado para activacion"));
  shell_print("staged=");
  shell_print(status.staged_version[0] ? status.staged_version : "-");
  shell_print(" cache_sha256=");
  shell_print(status.payload_cache_sha256[0] ? status.payload_cache_sha256 : "-");
  shell_print(" payload=");
  shell_print(status.staged_payload_url[0] ? status.staged_payload_url : "-");
  shell_newline();
  update_history_append_event("prepare", &status);
  return 0;
}

int cmd_update_prepare_dry_run(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-prepare-dry-run\nValida catalogo local e cache verificado que seriam usados no preparo, sem staging, arm ou apply.\n",
        "Usage: update-prepare-dry-run\nValidates the local catalog and verified cache that would be used for prepare, without staging, arm or apply.\n",
        "Uso: update-prepare-dry-run\nValida el catalogo local y cache verificado que se usarian en prepare, sin staging, arm ni apply.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-prepare-dry-run");
    return -1;
  }

  rc = refresh_update_agent_service_state(update_agent_prepare_dry_run(), &status);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "dry-run de preparo do update falhou",
        "update prepare dry-run failed",
        "dry-run de preparacion del update fallo"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(
      language,
      "dry-run de preparo aprovado",
      "update prepare dry-run passed",
      "dry-run de preparacion aprobado"));
  shell_print("available=");
  shell_print(status.available_version[0] ? status.available_version : "-");
  shell_print(" cache_sha256=");
  shell_print(status.payload_cache_sha256[0] ? status.payload_cache_sha256 : "-");
  shell_print(" payload=");
  shell_print(status.payload_url[0] ? status.payload_url : "-");
  shell_newline();
  return 0;
}

int cmd_update_prepare_explain(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct update_prepare_explain explain;
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-prepare-explain\nMostra os gates locais do preparo sem fetch, download, staging, arm ou apply.\n",
        "Usage: update-prepare-explain\nShows the local prepare gates without fetch, download, staging, arm or apply.\n",
        "Uso: update-prepare-explain\nMuestra los gates locales de preparacion sin fetch, descarga, staging, arm ni apply.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-prepare-explain");
    return -1;
  }

  rc = refresh_update_agent_service_state(update_agent_prepare_explain(&explain),
                                          &status);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "preflight de preparo encontrou bloqueios",
        "prepare preflight found blockers",
        "preflight de preparacion encontro bloqueos"));
  } else {
    shell_print_ok(localization_select(
        language,
        "preflight de preparo aprovado",
        "prepare preflight passed",
        "preflight de preparacion aprobado"));
  }
  shell_print("poll=");
  shell_print(update_gate_status(explain.poll_ready));
  shell_print(" catalog=");
  shell_print(update_gate_status(explain.catalog_ready));
  shell_print(" repository=");
  shell_print(update_gate_status(explain.repository_ready));
  shell_print(" version=");
  shell_print(update_gate_status(explain.version_ready));
  shell_print(" payload_sha=");
  shell_print(update_gate_status(explain.payload_sha256_ready));
  shell_print(" payload_url=");
  shell_print(update_gate_status(explain.payload_url_ready));
  shell_print(" signature=");
  shell_print(update_gate_status(explain.signature_ready));
  shell_print(" cache=");
  shell_print(update_gate_status(explain.cache_ready));
  shell_print(" stage_safe=");
  shell_print(update_gate_status(explain.stage_safe));
  shell_print(" failing=");
  shell_print(explain.failing_gate[0] ? explain.failing_gate : "-");
  shell_newline();
  shell_print(explain.summary);
  shell_newline();
  return rc < 0 ? -1 : 0;
}

int cmd_update_stage(struct shell_context *ctx, int argc, char **argv) {
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
  shell_print(" payload=");
  shell_print(status.staged_payload_url[0] ? status.staged_payload_url : "-");
  shell_newline();
  update_history_append_event("stage", &status);
  return 0;
}

int cmd_update_apply(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  const char *digest = NULL;
  int manual_digest = 0;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-apply [payload_sha256]\nAplica o staged update usando o cache verificado; se informado, usa o SHA-256 manual como fallback explicito.\n",
        "Usage: update-apply [payload_sha256]\nApplies the staged update using the verified cache; if provided, uses the manual SHA-256 as an explicit fallback.\n",
        "Uso: update-apply [payload_sha256]\nAplica el staged update usando el cache verificado; si se informa, usa el SHA-256 manual como fallback explicito.\n"));
    return 0;
  }
  if (argc != 1 && argc != 2) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-apply");
    return -1;
  }

  manual_digest = (argc == 2) ? 1 : 0;
  rc = refresh_update_agent_service_state(
      manual_digest ? update_agent_apply_boot_slot_verified(argv[1])
                    : update_agent_apply_cached_payload(),
      &status);
  if (rc < 0) {
    shell_print_error(localization_select(
        language,
        "nao foi possivel aplicar o staged update verificado",
        "could not apply the verified staged update",
        "no fue posible aplicar el staged update verificado"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  digest = manual_digest ? argv[1]
                         : (status.payload_cache_sha256[0]
                                ? status.payload_cache_sha256
                                : "-");
  shell_print_ok(localization_select(
      language,
      "staged update verificado e boot slot armado",
      "staged update verified and boot slot armed",
      "staged update verificado y boot slot armado"));
  shell_print("staged=");
  shell_print(status.staged_version[0] ? status.staged_version : "-");
  shell_print(" digest=");
  shell_print(digest);
  shell_print(" source=");
  shell_print(manual_digest ? "manual" : "cache");
  shell_newline();
  update_history_append_event("apply", &status);
  return 0;
}

int cmd_update_confirm_health(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-confirm-health\nConfirma que o boot atual esta saudavel e conclui o update transacional.\n",
        "Usage: update-confirm-health\nConfirms the current boot is healthy and commits the transactional update.\n",
        "Uso: update-confirm-health\nConfirma que el boot actual esta saludable y concluye el update transaccional.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-confirm-health");
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  update_agent_set_remover(update_runtime_remover);
  rc = refresh_update_agent_service_state(update_agent_confirm_health(), &status);
  update_agent_set_writer(NULL);
  update_agent_set_remover(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "nao foi possivel confirmar a saude do boot",
                                          "could not confirm boot health",
                                          "no fue posible confirmar la salud del boot"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "saude do boot confirmada",
                                     "boot health confirmed",
                                     "salud del boot confirmada"));
  shell_print(status.summary);
  shell_newline();
  update_history_append_event("confirm-health", &status);
  return 0;
}

int cmd_update_rollback_check(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_update_status status;
  int rc = 0;
  (void)ctx;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: update-rollback-check\nVerifica se ha rollback de boot pendente e executa rollback seguro quando necessario.\n",
        "Usage: update-rollback-check\nChecks for a pending boot rollback and performs a safe rollback when needed.\n",
        "Uso: update-rollback-check\nVerifica si hay rollback de boot pendiente y ejecuta rollback seguro cuando sea necesario.\n"));
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("update-rollback-check");
    return -1;
  }

  update_agent_set_writer(update_runtime_writer);
  update_agent_set_remover(update_runtime_remover);
  rc = refresh_update_agent_service_state(update_agent_check_rollback(), &status);
  update_agent_set_writer(NULL);
  update_agent_set_remover(NULL);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "nao foi possivel verificar rollback de boot",
                                          "could not check boot rollback",
                                          "no fue posible verificar rollback de boot"));
    shell_print(status.summary);
    shell_newline();
    return -1;
  }

  shell_print_ok(rc > 0 ? localization_select(language,
                                              "rollback de boot executado",
                                              "boot rollback completed",
                                              "rollback de boot ejecutado")
                        : localization_select(language,
                                              "nenhum rollback de boot pendente",
                                              "no boot rollback pending",
                                              "ningun rollback de boot pendiente"));
  shell_print(status.summary);
  shell_newline();
  update_history_append_event(rc > 0 ? "rollback" : "rollback-check", &status);
  return 0;
}

int cmd_update_arm(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_update_clear(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_update_import_manifest(struct shell_context *ctx, int argc, char **argv) {
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

int cmd_update_channel(struct shell_context *ctx, int argc, char **argv) {
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
