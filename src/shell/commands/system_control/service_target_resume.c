#include "internal/system_control_internal.h"

int cmd_service_target(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_service_target_status target;
  int target_id = -1;
  int rc = 0;

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: service-target [show|list|apply <nome>]\nMostra ou aplica o alvo atual do supervisor de servicos.\nO modo show exibe o alvo ativo e o alvo salvo.\n",
        "Usage: service-target [show|list|apply <name>]\nShows or applies the current service supervisor target.\nShow mode displays the active and saved targets.\n",
        "Uso: service-target [show|list|apply <nombre>]\nMuestra o aplica el objetivo actual del supervisor de servicios.\nEl modo show muestra el objetivo activo y el guardado.\n"));
    return 0;
  }

  if (argc < 2 || shell_string_equal(argv[1], "show")) {
    if (service_manager_target_current(&target) != 0) {
      shell_print_error(localization_select(language,
                                            "alvo de servico indisponivel",
                                            "service target unavailable",
                                            "objetivo de servicio no disponible"));
      return -1;
    }
    shell_print("active=");
    shell_print(target.name);
    if (ctx && ctx->settings && ctx->settings->service_target[0]) {
      shell_print(" saved=");
      shell_print(ctx->settings->service_target);
    }
    shell_print(" mask=");
    shell_print_number(target.service_mask);
    shell_newline();
    return 0;
  }

  if (shell_string_equal(argv[1], "list")) {
    size_t count = service_manager_target_count();
    for (size_t i = 0; i < count; ++i) {
      if (service_manager_target_get_at(i, &target) != 0) {
        continue;
      }
      shell_print(target.name);
      shell_print(" mask=");
      shell_print_number(target.service_mask);
      shell_newline();
    }
    return 0;
  }

  if (!shell_string_equal(argv[1], "apply") || argc < 3) {
    shell_print_error(localization_select(language,
                                          "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("service-target");
    return -1;
  }

  target_id = service_manager_target_find(argv[2], &target);
  if (target_id < 0) {
    shell_print_error(localization_select(language,
                                          "alvo de servico desconhecido",
                                          "unknown service target",
                                          "objetivo de servicio desconocido"));
    return -1;
  }

  rc = service_manager_target_apply((uint32_t)target_id);
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "falha ao aplicar alvo de servico",
                                          "failed to apply service target",
                                          "fallo al aplicar el objetivo de servicio"));
    return -1;
  }
  if (ctx && ctx->settings) {
    shell_copy(((struct system_settings *)ctx->settings)->service_target,
               sizeof(ctx->settings->service_target), target.name);
  }
  if (system_save_service_target(target.name) != 0) {
    shell_print_ok(localization_select(language,
                                       "alvo de servico aplicado",
                                       "service target applied",
                                       "objetivo de servicio aplicado"));
    shell_print(target.name);
    shell_newline();
    shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
    return 0;
  }
  shell_print_ok(localization_select(language,
                                     "alvo de servico aplicado",
                                     "service target applied",
                                     "objetivo de servicio aplicado"));
  shell_print(target.name);
  shell_newline();
  return 0;
}

int shell_resolve_recovery_target(struct shell_context *ctx,
                                  const char *requested_name,
                                  struct system_service_target_status *out,
                                  const char **resolved_name) {
  const char *target_name = requested_name;
  int target_id = -1;

  if (!target_name || shell_string_equal(target_name, "saved")) {
    target_name = (ctx && ctx->settings && ctx->settings->service_target[0])
                      ? ctx->settings->service_target
                      : "network";
  }
  if (resolved_name) {
    *resolved_name = target_name;
  }
  target_id = service_manager_target_find(target_name, out);
  return target_id;
}

int cmd_recovery_resume(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
#if defined(__x86_64__)
  struct x64_kernel_recovery_status status;
  struct system_service_target_status target;
  const char *target_name = NULL;
  int target_id = -1;
  int rc = 0;
#endif

  if (shell_help_requested(argc, argv) || argc < 2) {
    shell_print(localization_select(
        language,
        "Uso: recovery-resume <saved|core|network|full|maintenance>\nTenta promover o runtime atual para outro alvo de servicos com validacoes minimas de recuperacao.\n",
        "Usage: recovery-resume <saved|core|network|full|maintenance>\nAttempts to promote the current runtime to another service target with minimal recovery validation.\n",
        "Uso: recovery-resume <saved|core|network|full|maintenance>\nIntenta promover el runtime actual a otro objetivo de servicios con validaciones minimas de recuperacion.\n"));
    return argc < 2 ? -1 : 0;
  }

#if !defined(__x86_64__)
  (void)ctx;
  shell_print_error(localization_select(language,
                                        "recovery-resume indisponivel",
                                        "recovery-resume unavailable",
                                        "recovery-resume no disponible"));
  return -1;
#else
  x64_kernel_recovery_status_get(&status);
  if (!status.maintenance_session) {
    shell_print_error(localization_select(language,
                                          "o sistema nao esta em modo de recuperacao neste boot",
                                          "the system is not running in recovery mode for this boot",
                                          "el sistema no esta ejecutandose en modo de recuperacion en este arranque"));
    return -1;
  }

  target_id = shell_resolve_recovery_target(ctx, argv[1], &target, &target_name);
  if (target_id < 0) {
    shell_print_error(localization_select(language,
                                          "alvo de recuperacao desconhecido",
                                          "unknown recovery target",
                                          "objetivo de recuperacion desconocido"));
    return -1;
  }

  rc = x64_kernel_recovery_resume_target((uint32_t)target_id);
  if (rc == -2) {
    shell_print_error(localization_select(language,
                                          "storage validado ainda nao esta disponivel para sair do modo de recuperacao",
                                          "validated storage is still unavailable to leave recovery mode",
                                          "el almacenamiento validado aun no esta disponible para salir del modo de recuperacion"));
    return -1;
  }
  if (rc == -3) {
    shell_print_error(localization_select(language,
                                          "o estado da rede ainda nao pode ser validado",
                                          "network status cannot be validated yet",
                                          "el estado de la red aun no puede validarse"));
    return -1;
  }
  if (rc == -4) {
    shell_print_error(localization_select(language,
                                          "o runtime de rede validado ainda nao esta disponivel para este alvo",
                                          "validated network runtime is not available for this target yet",
                                          "el runtime de red validado aun no esta disponible para este objetivo"));
    return -1;
  }
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "falha ao promover o alvo de recuperacao",
                                          "failed to promote the recovery target",
                                          "fallo al promover el objetivo de recuperacion"));
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "alvo de recuperacao aplicado",
                                     "recovery target applied",
                                     "objetivo de recuperacion aplicado"));
  shell_print(target.name);
  shell_newline();
  shell_print(localization_select(
      language,
      "A sessao atual continua em modo de recuperacao ate reboot ou novo login.\n",
      "The current session remains in recovery mode until reboot or a new login.\n",
      "La sesion actual permanece en modo de recuperacion hasta reiniciar o realizar un nuevo inicio de sesion.\n"));
  return 0;
#endif
}
