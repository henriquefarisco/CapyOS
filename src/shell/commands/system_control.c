#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"
#include "core/klog_persist.h"
#include "core/service_manager.h"
#include "core/system_init.h"
#include "core/user_prefs.h"
#include "core/version.h"
#if defined(__x86_64__)
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/storage_runtime.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "drivers/hyperv/hyperv.h"
#endif
#include "drivers/acpi/acpi.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/buffer.h"
#include "fs/vfs.h"

static int cmd_config_keyboard(struct shell_context *ctx, int argc,
                               char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    if (shell_string_equal(language, "en")) {
      shell_print("Usage: config-keyboard [layout]\n"
                  "Without arguments it shows the available layouts.\n"
                  "With one argument (for example: config-keyboard br-abnt2) "
                  "it changes the active layout.\n"
                  "Subcommands: list, show.\n");
    } else if (shell_string_equal(language, "es")) {
      shell_print("Uso: config-keyboard [layout]\n"
                  "Sin argumentos muestra los layouts disponibles.\n"
                  "Con un argumento (por ejemplo: config-keyboard br-abnt2) "
                  "cambia el layout activo.\n"
                  "Subcomandos: list, show.\n");
    } else {
      shell_print("Uso: config-keyboard [layout]\n"
                  "Sem argumentos mostra os layouts disponiveis.\n"
                  "Com um argumento (ex.: config-keyboard br-abnt2) altera o "
                  "layout ativo.\n"
                  "Subcomandos: list, show.\n");
    }
    return 0;
  }
  if (argc < 2 || shell_string_equal(argv[1], "list")) {
    shell_print(localization_text_for(language, LOC_TEXT_LAYOUT_CURRENT));
    const char *cur = keyboard_current_layout();
    shell_print(cur ? cur : localization_select(language, "(desconhecido)",
                                                "(unknown)", "(desconocido)"));
    shell_newline();
    shell_print(localization_text_for(language, LOC_TEXT_LAYOUT_LIST_HEADER));
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
      shell_print(" - ");
      shell_print(keyboard_layout_name(i));
      shell_print(" : ");
      shell_print(keyboard_layout_description(i));
      shell_newline();
    }
    return 0;
  }
  if (shell_string_equal(argv[1], "show")) {
    shell_print(localization_text_for(language, LOC_TEXT_LAYOUT_CURRENT));
    const char *cur = keyboard_current_layout();
    shell_print(cur ? cur : localization_select(language, "(desconhecido)",
                                                "(unknown)", "(desconocido)"));
    shell_newline();
    return 0;
  }
  if (keyboard_set_layout_by_name(argv[1]) != 0) {
    shell_print_error(localization_text_for(language, LOC_TEXT_LAYOUT_UNKNOWN));
    shell_suggest_help("config-keyboard");
    return -1;
  }
  if (system_save_keyboard_layout(argv[1]) != 0) {
    shell_print_ok(localization_text_for(language, LOC_TEXT_LAYOUT_UPDATED));
    shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
  } else {
    shell_print_ok(localization_text_for(language,
                                         LOC_TEXT_LAYOUT_UPDATED_SAVED));
  }
  return 0;
}

static const char *theme_name_or_null(const char *name) {
  if (!name || !name[0]) {
    return NULL;
  }
  if (shell_string_equal(name, "capyos")) {
    return "capyos";
  }
  if (shell_string_equal(name, "ocean")) {
    return "ocean";
  }
  if (shell_string_equal(name, "forest")) {
    return "forest";
  }
  return NULL;
}

static const char *splash_state_text(const char *language, int enabled) {
  if (shell_string_equal(language, "en")) {
    return enabled ? "enabled" : "disabled";
  }
  if (shell_string_equal(language, "es")) {
    return enabled ? "habilitado" : "deshabilitado";
  }
  return enabled ? "habilitado" : "desabilitado";
}

static int parse_splash_mode(const char *value, int *enabled) {
  if (!value || !enabled) {
    return -1;
  }
  if (shell_string_equal(value, "on") || shell_string_equal(value, "enable") ||
      shell_string_equal(value, "enabled") || shell_string_equal(value, "1")) {
    *enabled = 1;
    return 0;
  }
  if (shell_string_equal(value, "off") || shell_string_equal(value, "disable") ||
      shell_string_equal(value, "disabled") || shell_string_equal(value, "0")) {
    *enabled = 0;
    return 0;
  }
  return -1;
}

static int cmd_config_theme(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  if (shell_help_requested(argc, argv)) {
    if (shell_string_equal(language, "en")) {
      shell_print("Usage: config-theme [theme]\n"
                  "Without arguments it shows the available themes.\n"
                  "With one argument (for example: config-theme ocean) it "
                  "changes the active theme and saves it in /system/config.ini.\n"
                  "Subcommands: list, show.\n");
    } else if (shell_string_equal(language, "es")) {
      shell_print("Uso: config-theme [tema]\n"
                  "Sin argumentos muestra los temas disponibles.\n"
                  "Con un argumento (por ejemplo: config-theme ocean) cambia "
                  "el tema activo y lo guarda en /system/config.ini.\n"
                  "Subcomandos: list, show.\n");
    } else {
      shell_print("Uso: config-theme [tema]\n"
                  "Sem argumentos mostra os temas disponiveis.\n"
                  "Com um argumento (ex.: config-theme ocean) altera o tema "
                  "ativo e salva em /system/config.ini.\n"
                  "Subcomandos: list, show.\n");
    }
    return 0;
  }

  if (argc < 2 || shell_string_equal(argv[1], "list")) {
    const char *current =
        (ctx && ctx->settings && ctx->settings->theme[0]) ? ctx->settings->theme
                                                          : "capyos";
    shell_print(localization_text_for(language, LOC_TEXT_THEME_CURRENT));
    shell_print(current);
    shell_newline();
    shell_print(localization_text_for(language, LOC_TEXT_THEME_LIST_HEADER));
    shell_print(localization_select(language,
                                    " - capyos : padrao verde/escuro do sistema\n",
                                    " - capyos : default green/dark system theme\n",
                                    " - capyos : tema verde/oscuro predeterminado del sistema\n"));
    shell_print(localization_select(language,
                                    " - ocean  : variante azul/ciano\n",
                                    " - ocean  : blue/cyan variant\n",
                                    " - ocean  : variante azul/cian\n"));
    shell_print(localization_select(language,
                                    " - forest : variante verde/floresta\n",
                                    " - forest : green/forest variant\n",
                                    " - forest : variante verde/bosque\n"));
    return 0;
  }

  if (shell_string_equal(argv[1], "show")) {
    const char *current =
        (ctx && ctx->settings && ctx->settings->theme[0]) ? ctx->settings->theme
                                                          : "capyos";
    shell_print(localization_text_for(language, LOC_TEXT_THEME_CURRENT));
    shell_print(current);
    shell_newline();
    return 0;
  }

  {
    const char *theme = theme_name_or_null(argv[1]);
    struct system_settings *mutable_settings = NULL;
    if (!theme) {
      shell_print_error(localization_text_for(language, LOC_TEXT_THEME_UNKNOWN));
      shell_suggest_help("config-theme");
      return -1;
    }

    if (ctx && ctx->settings) {
      mutable_settings = (struct system_settings *)ctx->settings;
      shell_copy(mutable_settings->theme, sizeof(mutable_settings->theme), theme);
      system_apply_theme(mutable_settings);
    }

    vga_clear();

    if (system_save_theme(theme) != 0) {
      shell_print_ok(localization_text_for(language, LOC_TEXT_THEME_UPDATED));
      shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
      return 0;
    }

    shell_print_ok(localization_text_for(language,
                                         LOC_TEXT_THEME_UPDATED_SAVED));
    return 0;
  }
}

static int cmd_config_splash(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  int current_enabled = 1;
  if (ctx && ctx->settings) {
    current_enabled = ctx->settings->splash_enabled ? 1 : 0;
  }

  if (shell_help_requested(argc, argv)) {
    if (shell_string_equal(language, "en")) {
      shell_print("Usage: config-splash [show|on|off]\n"
                  "Without arguments it shows the current state.\n"
                  "Use 'on' to enable the boot animation on the next boot.\n"
                  "Use 'off' to disable the animation and prioritize boot logs.\n");
    } else if (shell_string_equal(language, "es")) {
      shell_print("Uso: config-splash [show|on|off]\n"
                  "Sin argumentos muestra el estado actual.\n"
                  "Usa 'on' para habilitar la animacion inicial en el proximo arranque.\n"
                  "Usa 'off' para deshabilitar la animacion y priorizar los logs del arranque.\n");
    } else {
      shell_print("Uso: config-splash [show|on|off]\n"
                  "Sem argumentos mostra o estado atual.\n"
                  "Use 'on' para habilitar a animacao inicial no proximo boot.\n"
                  "Use 'off' para desabilitar a animacao e priorizar logs de boot.\n");
    }
    return 0;
  }

  if (argc < 2 || shell_string_equal(argv[1], "show")) {
    shell_print(localization_text_for(language, LOC_TEXT_SPLASH_CURRENT));
    shell_print(splash_state_text(language, current_enabled));
    shell_newline();
    return 0;
  }

  {
    int enabled = 0;
    struct system_settings *mutable_settings = NULL;
    if (parse_splash_mode(argv[1], &enabled) != 0) {
      shell_print_error(localization_text_for(language, LOC_TEXT_SPLASH_UNKNOWN));
      shell_suggest_help("config-splash");
      return -1;
    }

    if (ctx && ctx->settings) {
      mutable_settings = (struct system_settings *)ctx->settings;
      mutable_settings->splash_enabled = enabled;
    }

    if (system_save_splash_enabled(enabled) != 0) {
      shell_print_ok(localization_text_for(language, LOC_TEXT_SPLASH_UPDATED));
      shell_print(localization_text_for(language, LOC_TEXT_CONFIG_SAVE_WARNING));
      return 0;
    }

    shell_print_ok(enabled ? localization_text_for(language, LOC_TEXT_SPLASH_UPDATED_ON)
                           : localization_text_for(language, LOC_TEXT_SPLASH_UPDATED_OFF));
    return 0;
  }
}

static void print_language_list(const char *language) {
  shell_print(localization_text_for(language, LOC_TEXT_LANGUAGE_LIST_HEADER));
  shell_print(" - pt-BR : ");
  shell_print(localization_language_name("pt-BR"));
  shell_newline();
  shell_print(" - en    : ");
  shell_print(localization_language_name("en"));
  shell_newline();
  shell_print(" - es    : ");
  shell_print(localization_language_name("es"));
  shell_newline();
}

static int cmd_config_language(struct shell_context *ctx, int argc,
                               char **argv) {
  const struct user_record *user =
      ctx && ctx->session ? session_user(ctx->session) : NULL;
  const char *language = shell_current_language();

  if (shell_help_requested(argc, argv)) {
    if (shell_string_equal(language, "en")) {
      shell_print("Usage: config-language [list|show|pt-BR|en|es]\n"
                  "Changes the language for the current user and saves it in the home profile.\n");
    } else if (shell_string_equal(language, "es")) {
      shell_print("Uso: config-language [list|show|pt-BR|en|es]\n"
                  "Cambia el idioma del usuario actual y lo guarda en el perfil del home.\n");
    } else {
      shell_print("Uso: config-language [list|show|pt-BR|en|es]\n"
                  "Altera o idioma do usuario atual e salva no perfil do home.\n");
    }
    return 0;
  }

  if (argc < 2 || shell_string_equal(argv[1], "list")) {
    shell_print(localization_text_for(language, LOC_TEXT_LANGUAGE_CURRENT));
    shell_print(localization_language_label(language));
    shell_newline();
    print_language_list(language);
    return 0;
  }

  if (shell_string_equal(argv[1], "show")) {
    shell_print(localization_text_for(language, LOC_TEXT_LANGUAGE_CURRENT));
    shell_print(localization_language_label(language));
    shell_newline();
    return 0;
  }

  {
    const char *normalized = localization_normalize_language(argv[1]);
    if (!normalized) {
      shell_print_error(
          localization_text_for(language, LOC_TEXT_LANGUAGE_UNKNOWN));
      shell_suggest_help("config-language");
      return -1;
    }

    if (ctx && ctx->session) {
      shell_copy(ctx->session->prefs.language,
                 sizeof(ctx->session->prefs.language), normalized);
    }

    if (!user || user_prefs_save_language(user, normalized) != 0) {
      shell_print_ok(
          localization_text_for(normalized, LOC_TEXT_LANGUAGE_UPDATED));
      shell_print(localization_text_for(normalized, LOC_TEXT_CONFIG_SAVE_WARNING));
      return 0;
    }

    shell_print_ok(
        localization_text_for(normalized, LOC_TEXT_LANGUAGE_UPDATED_SAVED));
    return 0;
  }
}

static void sync_and_flush(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    (void)buffer_cache_sync(root->bdev);
  }
  (void)klog_persist_flush_default();
}

static int find_service_id_by_name(const char *name, struct system_service_status *out) {
  size_t count = service_manager_count();
  for (size_t i = 0; i < count; ++i) {
    struct system_service_status svc;
    if (service_manager_get_at(i, &svc) != 0) {
      continue;
    }
    if (shell_string_equal(name, svc.name)) {
      if (out) {
        *out = svc;
      }
      return (int)svc.id;
    }
  }
  return -1;
}

static int cmd_service_control(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_service_status svc;
  int service_id = -1;
  int rc = 0;

  (void)ctx;
  if (shell_help_requested(argc, argv) || argc < 3) {
    shell_print(localization_select(
        language,
        "Uso: service-control <start|stop|restart> <nome>\nControla o ciclo de vida basico dos servicos internos.\n",
        "Usage: service-control <start|stop|restart> <name>\nControls the basic lifecycle of internal services.\n",
        "Uso: service-control <start|stop|restart> <nombre>\nControla el ciclo de vida basico de los servicios internos.\n"));
    return argc < 3 ? -1 : 0;
  }

  service_id = find_service_id_by_name(argv[2], &svc);
  if (service_id < 0) {
    shell_print_error(localization_select(language,
                                          "servico desconhecido",
                                          "unknown service",
                                          "servicio desconocido"));
    return -1;
  }

  if (shell_string_equal(argv[1], "start")) {
    rc = service_manager_start((uint32_t)service_id);
  } else if (shell_string_equal(argv[1], "stop")) {
    rc = service_manager_stop((uint32_t)service_id);
  } else if (shell_string_equal(argv[1], "restart")) {
    rc = service_manager_restart((uint32_t)service_id);
  } else {
    shell_print_error(localization_select(language,
                                          "acao invalida",
                                          "invalid action",
                                          "accion invalida"));
    shell_suggest_help("service-control");
    return -1;
  }

  if (rc == -2) {
    shell_print_error(localization_select(language,
                                          "servico bloqueado por politica atual",
                                          "service blocked by current policy",
                                          "servicio bloqueado por la politica actual"));
    return -1;
  }
  if (rc == -3) {
    shell_print_error(localization_select(language,
                                          "dependencias do servico ainda nao estao prontas",
                                          "service dependencies are not ready yet",
                                          "las dependencias del servicio aun no estan listas"));
    return -1;
  }
  if (rc < 0) {
    shell_print_error(localization_select(language,
                                          "operacao do servico falhou",
                                          "service operation failed",
                                          "la operacion del servicio fallo"));
    return -1;
  }

  shell_print_ok(localization_select(language,
                                     "servico atualizado",
                                     "service updated",
                                     "servicio actualizado"));
  shell_print(argv[2]);
  shell_newline();
  return 0;
}

static int find_service_target_id_by_name(const char *name,
                                          struct system_service_target_status *out) {
  size_t count = service_manager_target_count();
  for (size_t i = 0; i < count; ++i) {
    struct system_service_target_status target;
    if (service_manager_target_get_at(i, &target) != 0) {
      continue;
    }
    if (shell_string_equal(name, target.name)) {
      if (out) {
        *out = target;
      }
      return (int)target.id;
    }
  }
  return -1;
}

static int cmd_service_target(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  struct system_service_target_status target;
  int target_id = -1;
  int rc = 0;

  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print(localization_select(
        language,
        "Uso: service-target [show|list|apply <nome>]\nMostra ou aplica o alvo atual do supervisor de servicos.\n",
        "Usage: service-target [show|list|apply <name>]\nShows or applies the current service supervisor target.\n",
        "Uso: service-target [show|list|apply <nombre>]\nMuestra o aplica el objetivo actual del supervisor de servicios.\n"));
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
    shell_print("target=");
    shell_print(target.name);
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

  target_id = find_service_target_id_by_name(argv[2], &target);
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
  shell_print_ok(localization_select(language,
                                     "alvo de servico aplicado",
                                     "service target applied",
                                     "objetivo de servicio aplicado"));
  shell_print(target.name);
  shell_newline();
  return 0;
}

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

static struct shell_command g_system_control_commands[10];
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
  g_system_control_commands[9].name = "service-target";
  g_system_control_commands[9].handler = cmd_service_target;
  g_system_control_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_control(size_t *count) {
  init_system_control_commands();
  if (count) {
    *count = 10;
  }
  return g_system_control_commands;
}
