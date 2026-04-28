#include "internal/system_control_internal.h"

static void do_hard_reboot(void) {
  const char *language = shell_current_language();
  sync_and_flush();
  shell_print(localization_select(language, "Reiniciando...\n",
                                  "Rebooting...\n", "Reiniciando...\n"));
  acpi_reboot();
}

static void do_power_off(void) {
  const char *language = shell_current_language();
  sync_and_flush();
  shell_print(localization_select(language, "Desligando...\n",
                                  "Powering off...\n",
                                  "Apagando...\n"));
  acpi_shutdown();
}

static int cmd_shutdown_reboot(struct shell_context *ctx, int argc,
                               char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: shutdown-reboot\nReinicia o sistema de forma controlada (sincroniza buffers).",
        "Usage: shutdown-reboot\nReboots the system in a controlled way (syncs buffers).",
        "Uso: shutdown-reboot\nReinicia el sistema de forma controlada (sincroniza buffers)."));
    shell_newline();
    return 0;
  }
  do_hard_reboot();
  return 0;
}

static int cmd_shutdown_off(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: shutdown-off\nDesliga o sistema (halt) apos sincronizar buffers.",
        "Usage: shutdown-off\nPowers off the system (halt) after syncing buffers.",
        "Uso: shutdown-off\nApaga el sistema (halt) despues de sincronizar buffers."));
    shell_newline();
    return 0;
  }
  do_power_off();
  return 0;
}

static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language, "Uso: do-sync\nForca a gravacao do buffer de disco.",
        "Usage: do-sync\nForces disk buffer flushing.",
        "Uso: do-sync\nFuerza la escritura del buffer de disco."));
    shell_newline();
    return 0;
  }
  (void)argc;
  (void)argv;
  struct super_block *root = vfs_root();
  if (!root || !root->bdev) {
    shell_print_error(localization_select(language, "sem dispositivo",
                                          "no device", "sin dispositivo"));
    return -1;
  }
  if (buffer_cache_sync(root->bdev) != 0) {
    shell_print_error(localization_select(language,
                                          "falha ao sincronizar buffers de disco",
                                          "failed to sync disk buffers",
                                          "fallo al sincronizar buffers de disco"));
    return -1;
  }
  if (klog_persist_flush_default() != 0) {
    shell_print_ok(localization_select(language, "buffers sincronizados",
                                       "buffers synced",
                                       "buffers sincronizados"));
    shell_print(localization_select(
        language,
        "Aviso: logs persistentes ainda nao puderam ser gravados.\n",
        "Warning: persistent logs could not be written yet.\n",
        "Aviso: los logs persistentes aun no pudieron escribirse.\n"));
    return 0;
  }
  shell_print_ok(localization_select(language, "buffers e logs sincronizados",
                                     "buffers and logs synced",
                                     "buffers y logs sincronizados"));
  return 0;
}

static void print_runtime_native_status(const char *language) {
  struct system_runtime_platform platform;
  system_runtime_platform_get(&platform);
  shell_print("build=");
  shell_print(CAPYOS_VERSION_FULL);
  shell_print(" feature=");
  shell_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
  shell_newline();
  shell_print(localization_select(language, "platform runtime: ",
                                  "platform runtime: ",
                                  "runtime de plataforma: "));
  shell_print(platform.boot_services_active ? "hybrid" : "native");
  shell_print(" bootsvc=");
  shell_print(platform.boot_services_active ? "active" : "inactive");
  shell_print(" ebs=");
  shell_print(system_exit_boot_services_gate_label(
      platform.exit_boot_services_gate));
  shell_print(" input-gate=");
  shell_print(system_hyperv_input_gate_label(platform.hyperv_input_gate));
  shell_print(" input-native=");
  shell_print(platform.native_input_ready ? "yes" : "no");
  shell_print(" storage-fw=");
  shell_print(platform.firmware_block_io_active ? "on" : "off");
  shell_print(" storage-native=");
  shell_print(platform.native_storage_ready ? "yes" : "no");
  shell_print(" storage-synth=");
  shell_print(platform.synthetic_storage_ready ? "yes" : "no");
  shell_print(" tables=");
#if defined(__x86_64__)
  shell_print(x64_platform_tables_status());
  shell_print(" vmbus=");
  shell_print(hyperv_vmbus_stage_label(platform.hyperv_vmbus_stage));
#else
  shell_print("n/a");
#endif
  if (platform.exit_boot_services_gate ==
          SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT &&
      platform.hyperv_input_gate == SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES) {
    shell_print(" deadlock=ebs-input-hyperv next=wait-runtime");
    } else if (platform.exit_boot_services_gate ==
               SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE) {
      if (!x64_storage_runtime_hyperv_bus_prepared()) {
        shell_print(" deadlock=storage-firmware-runtime next=prepare-storage");
      } else {
        shell_print(" deadlock=storage-firmware-runtime next=wait-runtime");
      }
    } else if (platform.exit_boot_services_gate ==
               SYSTEM_EXIT_BOOT_SERVICES_GATE_READY) {
      shell_print(" next=exit-boot-services");
  } else if (platform.hyperv_input_gate == SYSTEM_HYPERV_INPUT_GATE_PREPARED) {
    shell_print(" next=exit-boot-services");
  }
  shell_newline();
}

static int cmd_runtime_native(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: runtime-native [show|prepare-input|prepare-storage|exit-boot-services|step]\nMostra o gate do runtime nativo e executa passos manuais controlados do coordenador Hyper-V.\nNo modo hibrido, prepare-bridge, prepare-synic e prepare-input seguem desativados porque a validacao em Hyper-V real mostrou reboot.\n",
        "Usage: runtime-native [show|prepare-input|prepare-storage|exit-boot-services|step]\nShows the native-runtime gate and executes controlled manual steps of the Hyper-V coordinator.\nIn hybrid mode, prepare-bridge, prepare-synic and prepare-input remain disabled because real Hyper-V validation showed reboots.\n",
        "Uso: runtime-native [show|prepare-input|prepare-storage|exit-boot-services|step]\nMuestra el gate del runtime nativo y ejecuta pasos manuales controlados del coordinador Hyper-V.\nEn modo hibrido, prepare-bridge, prepare-synic y prepare-input siguen desactivados porque la validacion real en Hyper-V mostro reinicios.\n"));
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(localization_select(language,
                                        "runtime nativo indisponivel",
                                        "native runtime unavailable",
                                        "runtime nativo no disponible"));
  return -1;
#else
  if (argc < 2 || shell_string_equal(argv[1], "show")) {
    print_runtime_native_status(language);
    return 0;
  }
  if (shell_string_equal(argv[1], "prepare-input")) {
    struct system_runtime_platform platform;
    int rc = 0;
    system_runtime_platform_get(&platform);
    if (platform.boot_services_active) {
      shell_print_ok(localization_select(
          language,
          "prepare-input desativado no modo hibrido: a validacao em Hyper-V real mostrou reboot mesmo no preparo minimo",
          "prepare-input disabled in hybrid mode: real Hyper-V validation showed reboot even in the minimal preparation path",
          "prepare-input desactivado en modo hibrido: la validacion real en Hyper-V mostro reinicio incluso en la preparacion minima"));
    } else {
      rc = x64_kernel_manual_prepare_hyperv_input();
      if (rc > 0) {
        shell_print_ok(localization_select(
            language,
            "input Hyper-V preparado em runtime nativo",
            "Hyper-V input prepared in native runtime",
            "entrada Hyper-V preparada en runtime nativo"));
      } else if (rc < 0) {
        shell_print_error(localization_select(
            language,
            "falha ao preparar o input Hyper-V manualmente",
            "failed to prepare Hyper-V input manually",
            "fallo al preparar la entrada Hyper-V manualmente"));
      } else {
        shell_print_ok(localization_select(
            language,
            "nenhum preparo manual de input era necessario neste momento",
            "no manual input preparation was needed right now",
            "no era necesario preparar manualmente la entrada en este momento"));
      }
    }
    shell_newline();
    print_runtime_native_status(language);
    return rc < 0 ? -1 : 0;
  }
  if (shell_string_equal(argv[1], "prepare-bridge")) {
    shell_print_ok(localization_select(
        language,
        "prepare-bridge desativado no modo hibrido: validacao em Hyper-V real mostrou reboot apos armar descritores nativos antes do EBS",
        "prepare-bridge disabled in hybrid mode: real Hyper-V validation showed reboot after arming native descriptors before EBS",
        "prepare-bridge desactivado en modo hibrido: la validacion real en Hyper-V mostro reinicio tras armar descriptores nativos antes del EBS"));
    shell_newline();
    print_runtime_native_status(language);
    return 0;
  }
  if (shell_string_equal(argv[1], "prepare-synic")) {
    shell_print_ok(localization_select(
        language,
        "prepare-synic desativado no modo hibrido: a fase SynIC sera reintroduzida apenas depois de estabilizar o runtime sem reboot",
        "prepare-synic disabled in hybrid mode: the SynIC phase will only be reintroduced after the runtime is stable without reboot",
        "prepare-synic desactivado en modo hibrido: la fase SynIC solo se reintroducira despues de estabilizar el runtime sin reinicios"));
    shell_newline();
    print_runtime_native_status(language);
    return 0;
  }
  if (shell_string_equal(argv[1], "exit-boot-services")) {
    int rc = x64_kernel_manual_try_exit_boot_services();
    if (rc > 0) {
      shell_print_ok(localization_select(
          language,
          "ExitBootServices avancou em passo manual controlado",
          "ExitBootServices advanced in a controlled manual step",
          "ExitBootServices avanzo en un paso manual controlado"));
    } else if (rc < 0) {
      shell_print_error(localization_select(
          language,
          "a tentativa manual de ExitBootServices falhou",
          "the manual ExitBootServices attempt failed",
          "el intento manual de ExitBootServices fallo"));
    } else {
      shell_print_ok(localization_select(
          language,
          "ExitBootServices ainda nao possui pre-requisitos seguros",
          "ExitBootServices still lacks safe prerequisites",
          "ExitBootServices aun no tiene prerequisitos seguros"));
    }
    shell_newline();
    print_runtime_native_status(language);
    return rc < 0 ? -1 : 0;
  }
    if (shell_string_equal(argv[1], "prepare-storage")) {
      int rc = x64_kernel_manual_prepare_hyperv_storage();
      if (rc > 0) {
        shell_print_ok(localization_select(
            language,
            "storage Hyper-V executou apenas o passo seguro disponivel",
            "Hyper-V storage executed only the safe step currently available",
            "el almacenamiento Hyper-V ejecuto solo el paso seguro disponible"));
      } else if (rc < 0) {
        shell_print_error(localization_select(
            language,
          "falha ao avancar o storage Hyper-V manualmente",
          "failed to advance Hyper-V storage manually",
          "fallo al avanzar manualmente el almacenamiento Hyper-V"));
    } else {
      shell_print_ok(localization_select(
          language,
          "nenhum passo seguro de storage estava disponivel neste momento",
          "no safe storage step was available right now",
          "no habia un paso seguro de almacenamiento disponible en este momento"));
    }
    shell_newline();
    print_runtime_native_status(language);
    return rc < 0 ? -1 : 0;
  }
  if (!shell_string_equal(argv[1], "step")) {
    shell_print_error(localization_select(language, "uso invalido",
                                          "invalid usage",
                                          "uso invalido"));
    shell_suggest_help("runtime-native");
    return -1;
  }

  {
    int rc = x64_kernel_manual_native_runtime_step();
    if (rc > 0) {
      shell_print_ok(localization_select(
          language,
          "runtime nativo avancou em um passo controlado",
          "native runtime advanced by one controlled step",
          "el runtime nativo avanzo un paso controlado"));
    } else if (rc < 0) {
      shell_print_error(localization_select(
          language,
          "o passo controlado falhou; verifique o gate e o dump de runtime",
          "the controlled step failed; inspect the gate and runtime dump",
          "el paso controlado fallo; revisa el gate y el dump del runtime"));
    } else {
      shell_print_ok(localization_select(
          language,
          "nenhum passo seguro disponivel no momento",
          "no safe runtime step available right now",
          "no hay un paso seguro disponible en este momento"));
    }
  }
  shell_newline();
  print_runtime_native_status(language);
  return 0;
#endif
}

static struct shell_command g_system_control_commands[21];
static int g_system_control_commands_initialized = 0;

static void init_system_control_commands(void) {
  if (g_system_control_commands_initialized) {
    return;
  }
  g_system_control_commands[0].name = "config-keyboard";
  g_system_control_commands[0].handler = cmd_config_keyboard;
  g_system_control_commands[1].name = "config-theme";
  g_system_control_commands[1].handler = cmd_config_theme;
  g_system_control_commands[2].name = "config-splash";
  g_system_control_commands[2].handler = cmd_config_splash;
  g_system_control_commands[3].name = "config-language";
  g_system_control_commands[3].handler = cmd_config_language;
  g_system_control_commands[4].name = "shutdown-reboot";
  g_system_control_commands[4].handler = cmd_shutdown_reboot;
  g_system_control_commands[5].name = "shutdown-off";
  g_system_control_commands[5].handler = cmd_shutdown_off;
  g_system_control_commands[6].name = "do-sync";
  g_system_control_commands[6].handler = cmd_do_sync;
  g_system_control_commands[7].name = "runtime-native";
  g_system_control_commands[7].handler = cmd_runtime_native;
  g_system_control_commands[8].name = "service-control";
  g_system_control_commands[8].handler = cmd_service_control;
  g_system_control_commands[9].name = "job-run";
  g_system_control_commands[9].handler = cmd_job_run;
  g_system_control_commands[10].name = "update-check";
  g_system_control_commands[10].handler = cmd_update_check;
  g_system_control_commands[11].name = "update-stage";
  g_system_control_commands[11].handler = cmd_update_stage;
  g_system_control_commands[12].name = "update-arm";
  g_system_control_commands[12].handler = cmd_update_arm;
  g_system_control_commands[13].name = "update-clear";
  g_system_control_commands[13].handler = cmd_update_clear;
  g_system_control_commands[14].name = "update-import-manifest";
  g_system_control_commands[14].handler = cmd_update_import_manifest;
  g_system_control_commands[15].name = "update-channel";
  g_system_control_commands[15].handler = cmd_update_channel;
  g_system_control_commands[16].name = "service-target";
  g_system_control_commands[16].handler = cmd_service_target;
  g_system_control_commands[17].name = "recovery-resume";
  g_system_control_commands[17].handler = cmd_recovery_resume;
  g_system_control_commands[18].name = "recovery-verify";
  g_system_control_commands[18].handler = cmd_recovery_verify;
  g_system_control_commands[19].name = "recovery-login";
  g_system_control_commands[19].handler = cmd_recovery_login;
  g_system_control_commands[20].name = "recovery-storage-repair";
  g_system_control_commands[20].handler = cmd_recovery_storage_repair;
  g_system_control_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_control(size_t *count) {
  init_system_control_commands();
  if (count) {
    *count = 21;
  }
  return g_system_control_commands;
}
