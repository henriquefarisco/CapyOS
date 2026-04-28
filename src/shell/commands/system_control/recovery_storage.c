#include "internal/system_control_internal.h"

int cmd_recovery_storage_repair(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();

  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: recovery-storage-repair [reset-admin <senha>]\nReconstrui a base persistente minima (/system, /etc, /var/log, config.ini e users.db) quando o volume ja esta montado. Use reset-admin <senha> para recriar ou redefinir a conta admin durante a recuperacao.\n",
        "Usage: recovery-storage-repair [reset-admin <password>]\nRebuilds the minimum persistent base (/system, /etc, /var/log, config.ini and users.db) when the volume is already mounted. Use reset-admin <password> to recreate or reset the admin account during recovery.\n",
        "Uso: recovery-storage-repair [reset-admin <clave>]\nReconstruye la base persistente minima (/system, /etc, /var/log, config.ini y users.db) cuando el volumen ya esta montado. Usa reset-admin <clave> para recrear o restablecer la cuenta admin durante la recuperacion.\n"));
    return 0;
  }

#if !defined(__x86_64__)
  (void)ctx;
  shell_print_error(localization_select(language,
                                        "recovery-storage-repair indisponivel",
                                        "recovery-storage-repair unavailable",
                                        "recovery-storage-repair no disponible"));
  return -1;
#else
  {
    struct x64_kernel_recovery_status status;
    struct capyfs_check_report fs_report;
    const char *admin_password = NULL;
    int admin_exists = 0;
    int fs_ok = 0;

    x64_kernel_recovery_status_get(&status);
    if (!status.maintenance_session) {
      shell_print_error(localization_select(
          language,
          "recovery-storage-repair so pode ser usado no modo de recuperacao",
          "recovery-storage-repair can only be used in recovery mode",
          "recovery-storage-repair solo puede usarse en modo de recuperacion"));
      return -1;
    }
    if (!status.shell_fs_ready) {
      shell_print_error(localization_select(
          language,
          "o VFS atual ainda nao esta pronto para reparo",
          "the current VFS is not ready for repair yet",
          "el VFS actual aun no esta listo para reparacion"));
      return -1;
    }
    if (status.recovery_ram_fallback || !status.persistent_storage) {
      shell_print_error(localization_select(
          language,
          "o shell de recuperacao esta em RAM temporaria; corrija o volume/chave pela ISO antes de tentar regravar a base persistente",
          "the recovery shell is running on temporary RAM; fix the volume/key from the ISO before trying to rewrite the persistent base",
          "la shell de recuperacion se esta ejecutando en RAM temporal; corrige el volumen/la clave desde la ISO antes de reescribir la base persistente"));
      return -1;
    }
    fs_ok = shell_recovery_capyfs_check(&fs_report) == 0 &&
            fs_report.result == CAPYFS_CHECK_OK;
    if (!fs_ok) {
      shell_print_error(localization_select(
          language,
          "o volume persistente montou, mas a estrutura CAPYFS esta inconsistente; use recovery-storage-check e recupere via ISO antes de regravar a base",
          "the persistent volume mounted, but the CAPYFS structure is inconsistent; use recovery-storage-check and recover via ISO before rewriting the base",
          "el volumen persistente se monto, pero la estructura CAPYFS es inconsistente; usa recovery-storage-check y recupera via ISO antes de reescribir la base"));
      return -1;
    }
    if (argc >= 2) {
      if (!shell_string_equal(argv[1], "reset-admin") || argc < 3) {
        shell_print_error(localization_select(language,
                                              "uso invalido",
                                              "invalid usage",
                                              "uso invalido"));
        shell_suggest_help("recovery-storage-repair");
        return -1;
      }
      admin_password = argv[2];
    }

    if (recovery_storage_ensure_base_layout() != 0) {
      shell_print_error(localization_select(
          language,
          "falha ao reconstruir a arvore minima de diretorios",
          "failed to rebuild the minimum directory tree",
          "fallo al reconstruir el arbol minimo de directorios"));
      return -1;
    }
    if (recovery_storage_rewrite_config(ctx) != 0) {
      shell_print_error(localization_select(
          language,
          "falha ao regravar /system/config.ini",
          "failed to rewrite /system/config.ini",
          "fallo al reescribir /system/config.ini"));
      return -1;
    }
    if (userdb_ensure() != 0) {
      shell_print_error(localization_select(
          language,
          "falha ao preparar /etc/users.db",
          "failed to prepare /etc/users.db",
          "fallo al preparar /etc/users.db"));
      return -1;
    }
    if (admin_password && recovery_storage_reset_admin(admin_password) != 0) {
      shell_print_error(localization_select(
          language,
          "falha ao recriar/redefinir a conta admin",
          "failed to recreate/reset the admin account",
          "fallo al recrear/restablecer la cuenta admin"));
      return -1;
    }

    admin_exists = userdb_find("admin", NULL) == 0;
    sync_and_flush();

    shell_print_ok(localization_select(
        language,
        "base persistente revalidada",
        "persistent base revalidated",
        "base persistente revalidada"));
    shell_print("config=/system/config.ini repo=/system/update/repository.ini users.db=");
    shell_print(admin_exists ? "ready" : "present-without-admin");
    if (admin_password) {
      shell_print(" admin=reset");
    }
    shell_newline();
    if (!admin_exists) {
      shell_print(localization_select(
          language,
          "Obs: a conta admin ainda nao existe. Use recovery-storage-repair reset-admin <senha> antes de sair da recuperacao.\n",
          "Note: the admin account still does not exist. Use recovery-storage-repair reset-admin <password> before leaving recovery.\n",
          "Nota: la cuenta admin aun no existe. Usa recovery-storage-repair reset-admin <clave> antes de salir de recuperacion.\n"));
    }
    return 0;
  }
#endif
}
