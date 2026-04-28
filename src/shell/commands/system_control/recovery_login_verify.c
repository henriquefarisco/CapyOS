#include "internal/system_control_internal.h"
#include "kernel/log/klog.h"

int cmd_recovery_login(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
#if defined(__x86_64__)
  struct x64_kernel_recovery_status status;
  struct system_service_target_status target;
  const char *target_name = NULL;
  int target_id = -1;
  int rc = 0;
#endif

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: recovery-login [saved|core|network|full]\nPromove o alvo indicado e encerra a sessao de recuperacao, retornando ao login normal sem reboot.\n",
        "Usage: recovery-login [saved|core|network|full]\nPromotes the selected target and leaves recovery, returning to the normal login without reboot.\n",
        "Uso: recovery-login [saved|core|network|full]\nPromueve el objetivo indicado y cierra la sesion de recuperacion, volviendo al inicio de sesion normal sin reiniciar.\n"));
    return 0;
  }

#if !defined(__x86_64__)
  (void)ctx;
  shell_print_error(localization_select(language,
                                        "recovery-login indisponivel",
                                        "recovery-login unavailable",
                                        "recovery-login no disponible"));
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

  target_id = shell_resolve_recovery_target(ctx,
                                            (argc >= 2) ? argv[1] : "saved",
                                            &target, &target_name);
  if (target_id < 0 || (uint32_t)target_id == SYSTEM_SERVICE_TARGET_MAINTENANCE) {
    shell_print_error(localization_select(language,
                                          "alvo de login de recuperacao invalido",
                                          "invalid recovery login target",
                                          "objetivo invalido para salir de recuperacion"));
    return -1;
  }

  rc = x64_kernel_recovery_request_normal_login((uint32_t)target_id);
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
                                          "falha ao sair do modo de recuperacao",
                                          "failed to leave recovery mode",
                                          "fallo al salir del modo de recuperacion"));
    return -1;
  }

  klog(KLOG_INFO, "[recovery] Recovery login: returning to normal mode.");
  shell_print_ok(localization_select(language,
                                     "retornando ao login normal com alvo",
                                     "returning to the normal login with target",
                                     "volviendo al inicio de sesion normal con el objetivo"));
  shell_print(target.name);
  shell_newline();
  return 0;
#endif
}

int cmd_recovery_verify(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
#if defined(__x86_64__)
  struct x64_kernel_recovery_status status;
  struct system_service_target_status target;
  struct capyfs_check_report fs_report;
  const char *target_name = NULL;
  struct net_stack_status net_status;
  int target_id = -1;
  int net_rc = -1;
  int storage_ok = 0;
  int network_ok = 0;
  int fs_ok = 0;
  int verify_ok = 1;
#endif

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: recovery-verify [saved|core|network|full|maintenance]\nVerifica se os prerequisitos minimos para promover um target de recuperacao ja estao presentes.\n",
        "Usage: recovery-verify [saved|core|network|full|maintenance]\nChecks whether the minimum prerequisites to promote a recovery target are already present.\n",
        "Uso: recovery-verify [saved|core|network|full|maintenance]\nVerifica si los prerequisitos minimos para promover un objetivo de recuperacion ya estan presentes.\n"));
    return 0;
  }

#if !defined(__x86_64__)
  (void)ctx;
  shell_print_error(localization_select(language,
                                        "recovery-verify indisponivel",
                                        "recovery-verify unavailable",
                                        "recovery-verify no disponible"));
  return -1;
#else
  x64_kernel_recovery_status_get(&status);
  if (argc < 2 || shell_string_equal(argv[1], "saved")) {
    target_name = (ctx && ctx->settings && ctx->settings->service_target[0])
                      ? ctx->settings->service_target
                      : "network";
  } else {
    target_name = argv[1];
  }

  target_id = service_manager_target_find(target_name, &target);
  if (target_id < 0) {
    shell_print_error(localization_select(language,
                                          "alvo de verificacao desconhecido",
                                          "unknown verification target",
                                          "objetivo de verificacion desconocido"));
    return -1;
  }

  storage_ok = status.shell_fs_ready && status.persistent_storage &&
               x64_storage_runtime_has_device();
  fs_ok = shell_recovery_capyfs_check(&fs_report) == 0 &&
          fs_report.result == CAPYFS_CHECK_OK;
  net_rc = net_stack_status(&net_status);
  network_ok = (net_rc == 0 && net_status.runtime_supported);

  shell_print("target=");
  shell_print(target.name);
  shell_print(" maintenance=");
  shell_print(status.maintenance_session ? "yes" : "no");
  shell_newline();

  shell_print("check storage=");
  shell_print(storage_ok ? "ok" : "fail");
  shell_print(" fs=");
  shell_print(status.shell_fs_ready ? "ready" : "missing");
  shell_print(" persistent=");
  shell_print(status.persistent_storage ? "yes" : "no");
  shell_print(" capyfs=");
  shell_print(fs_ok ? "ok" : "fail");
  shell_print(" validated=");
  shell_print(x64_storage_runtime_has_device() ? "yes" : "no");
  shell_newline();

  shell_print("check network=");
  if (net_rc != 0) {
    shell_print("unknown");
  } else {
    shell_print(network_ok ? "ok" : "fail");
    shell_print(" driver=");
    shell_print(net_driver_name(net_status.nic.kind));
    shell_print(" ready=");
    shell_print(net_status.ready ? "yes" : "no");
    shell_print(" validated=");
    shell_print(net_status.runtime_supported ? "yes" : "no");
  }
  shell_newline();

  if ((uint32_t)target_id != SYSTEM_SERVICE_TARGET_MAINTENANCE &&
      (!storage_ok || !fs_ok)) {
    verify_ok = 0;
  }
  if (((uint32_t)target_id == SYSTEM_SERVICE_TARGET_NETWORK ||
       (uint32_t)target_id == SYSTEM_SERVICE_TARGET_FULL) &&
      !network_ok) {
    verify_ok = 0;
  }

  shell_print("result=");
  shell_print(verify_ok ? "ready" : "blocked");
  shell_newline();
  shell_print("next: ");
  if (verify_ok) {
    if ((uint32_t)target_id == SYSTEM_SERVICE_TARGET_MAINTENANCE) {
      shell_print("recovery-resume ");
    } else {
      shell_print("recovery-login ");
    }
    shell_print(target.name);
  } else if (!storage_ok || !fs_ok) {
    shell_print(localization_select(
        language,
        "corrija storage com recovery-storage/recovery-storage-check e, se a estrutura estiver saudavel, use recovery-storage-repair antes de sair do modo de recuperacao",
        "fix storage with recovery-storage/recovery-storage-check and, if the structure is healthy, use recovery-storage-repair before leaving recovery mode",
        "corrige el almacenamiento con recovery-storage/recovery-storage-check y, si la estructura esta sana, usa recovery-storage-repair antes de salir del modo de recuperacion"));
  } else {
    shell_print(localization_select(
        language,
        "corrija a rede com recovery-network ou ajuste a VM para hardware suportado antes de promover o target",
        "fix networking with recovery-network or adjust the VM to supported hardware before promoting the target",
        "corrige la red con recovery-network o ajusta la VM a hardware soportado antes de promover el objetivo"));
  }
  shell_newline();
  return verify_ok ? 0 : -1;
#endif
}
