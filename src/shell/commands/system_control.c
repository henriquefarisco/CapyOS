#include "shell/commands.h"
#include "shell/core.h"

#include "core/localization.h"
#include "core/klog_persist.h"
#include "core/service_manager.h"
#include "core/system_init.h"
#include "core/update_agent.h"
#include "core/user.h"
#include "core/user_prefs.h"
#include "core/version.h"
#include "core/work_queue.h"
#if defined(__x86_64__)
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/storage_runtime.h"
#include "drivers/timer/pit.h"
#include "net/stack.h"
#include "drivers/hyperv/hyperv.h"
#endif
#include "drivers/acpi/acpi.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/capyfs.h"
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

static void append_text(char *dst, size_t dst_size, const char *src) {
  size_t len = 0;
  size_t i = 0;
  if (!dst || dst_size == 0 || !src) {
    return;
  }
  while (dst[len] && len + 1 < dst_size) {
    ++len;
  }
  while (src[i] && len + 1 < dst_size) {
    dst[len++] = src[i++];
  }
  dst[len] = '\0';
}

static size_t text_length(const char *text) {
  size_t len = 0;
  if (!text) {
    return 0;
  }
  while (text[len]) {
    ++len;
  }
  return len;
}

static void append_u32_text(char *dst, size_t dst_size, uint32_t value) {
  char digits[16];
  size_t pos = 0;
  if (value == 0u) {
    append_text(dst, dst_size, "0");
    return;
  }
  while (value && pos + 1u < sizeof(digits)) {
    digits[pos++] = (char)('0' + (value % 10u));
    value /= 10u;
  }
  while (pos > 0u) {
    char ch[2];
    ch[0] = digits[--pos];
    ch[1] = '\0';
    append_text(dst, dst_size, ch);
  }
}

static void update_history_append_event(const char *event_name,
                                        const struct system_update_status *status) {
  char line[320];
  struct dentry *d = NULL;
  struct file *f = NULL;

  if (!status) {
    return;
  }
#if defined(__x86_64__)
  if (x64_kernel_volume_runtime_ensure_dir_recursive("/var/log") != 0) {
    return;
  }
#endif
  if (vfs_lookup("/var/log/update-history.log", &d) != 0 &&
      vfs_create("/var/log/update-history.log", VFS_MODE_FILE, NULL) != 0) {
    return;
  }

  line[0] = '\0';
#if defined(__x86_64__)
  append_text(line, sizeof(line), "ticks=");
  append_u32_text(line, sizeof(line), pit_ticks());
  append_text(line, sizeof(line), " ");
#endif
  append_text(line, sizeof(line), "event=");
  append_text(line, sizeof(line),
              (event_name && event_name[0]) ? event_name : "unknown");
  append_text(line, sizeof(line), " catalog=");
  append_text(line, sizeof(line), status->catalog_present ? "present" : "missing");
  append_text(line, sizeof(line), " update=");
  append_text(line, sizeof(line), status->update_available ? "available" : "none");
  append_text(line, sizeof(line), " stage=");
  append_text(line, sizeof(line), status->stage_ready ? "ready" : "empty");
  append_text(line, sizeof(line), " pending=");
  append_text(line, sizeof(line), status->pending_activation ? "armed" : "no");
  append_text(line, sizeof(line), " current=");
  append_text(line, sizeof(line),
              status->current_version[0] ? status->current_version : "-");
  append_text(line, sizeof(line), " available_ver=");
  append_text(line, sizeof(line),
              status->available_version[0] ? status->available_version : "-");
  append_text(line, sizeof(line), " staged_ver=");
  append_text(line, sizeof(line),
              status->staged_version[0] ? status->staged_version : "-");
  append_text(line, sizeof(line), " summary=");
  append_text(line, sizeof(line), status->summary[0] ? status->summary : "-");
  append_text(line, sizeof(line), "\n");

  f = vfs_open("/var/log/update-history.log", VFS_OPEN_WRITE);
  if (!f) {
    return;
  }
  if (f->dentry && f->dentry->inode) {
    f->position = f->dentry->inode->size;
  }
  (void)vfs_write(f, line, text_length(line));
  vfs_close(f);
}

static int shell_recovery_capyfs_check(struct capyfs_check_report *out) {
  struct super_block *root = vfs_root();
  if (!root || !root->bdev || !out) {
    return -1;
  }
  return capyfs_check(root->bdev, out);
}

#if defined(__x86_64__)
static int recovery_storage_ensure_base_layout(void) {
  static const char *dirs[] = {
      "/docs", "/etc", "/home", "/home/admin", "/system", "/tmp", "/var",
      "/var/log"};

  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
    if (x64_kernel_volume_runtime_ensure_dir_recursive(dirs[i]) != 0) {
      return -1;
    }
  }
  if (system_prepare_update_catalog() != 0) {
    return -1;
  }
  return 0;
}

static int recovery_storage_rewrite_config(const struct shell_context *ctx) {
  struct system_settings settings;

  if (ctx && ctx->settings) {
    settings = *ctx->settings;
  } else if (system_load_settings(&settings) != 0) {
    /* system_load_settings() already populates defaults on failure. */
  }

  return system_save_settings(&settings);
}

static int recovery_storage_reset_admin(const char *password) {
  struct user_record admin;
  uint32_t uid = 1000u;
  uint32_t gid = 1000u;

  if (!password || !password[0]) {
    return -1;
  }
  if (x64_kernel_volume_runtime_ensure_dir_recursive("/home/admin") != 0) {
    return -1;
  }
  if (userdb_find("admin", &admin) == 0) {
    return userdb_set_password("admin", password);
  }
  (void)userdb_next_ids(&uid, &gid);
  if (user_record_init("admin", password, "admin", uid, gid, "/home/admin",
                       &admin) != 0) {
    return -1;
  }
  return userdb_add(&admin);
}
#endif

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

  rc = refresh_update_agent_service_state(update_agent_stage_latest(), &status);
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

  rc = refresh_update_agent_service_state(
      update_agent_set_pending_activation(enable), &status);
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

static int cmd_service_target(struct shell_context *ctx, int argc, char **argv) {
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

static int shell_resolve_recovery_target(struct shell_context *ctx,
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

static int cmd_recovery_resume(struct shell_context *ctx, int argc, char **argv) {
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

static int cmd_recovery_login(struct shell_context *ctx, int argc, char **argv) {
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

  shell_print_ok(localization_select(language,
                                     "retornando ao login normal com alvo",
                                     "returning to the normal login with target",
                                     "volviendo al inicio de sesion normal con el objetivo"));
  shell_print(target.name);
  shell_newline();
  return 0;
#endif
}

static int cmd_recovery_verify(struct shell_context *ctx, int argc, char **argv) {
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

static int cmd_recovery_storage_repair(struct shell_context *ctx, int argc,
                                       char **argv) {
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

static struct shell_command g_system_control_commands[19];
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
  g_system_control_commands[14].name = "service-target";
  g_system_control_commands[14].handler = cmd_service_target;
  g_system_control_commands[15].name = "recovery-resume";
  g_system_control_commands[15].handler = cmd_recovery_resume;
  g_system_control_commands[16].name = "recovery-verify";
  g_system_control_commands[16].handler = cmd_recovery_verify;
  g_system_control_commands[17].name = "recovery-login";
  g_system_control_commands[17].handler = cmd_recovery_login;
  g_system_control_commands[18].name = "recovery-storage-repair";
  g_system_control_commands[18].handler = cmd_recovery_storage_repair;
  g_system_control_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_control(size_t *count) {
  init_system_control_commands();
  if (count) {
    *count = 19;
  }
  return g_system_control_commands;
}
