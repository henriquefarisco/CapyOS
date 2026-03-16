#include "shell/commands.h"
#include "shell/core.h"

#include "arch/x86/hw/io.h"
#include "core/localization.h"
#include "core/system_init.h"
#include "core/user_prefs.h"
#include "drivers/acpi/acpi.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/buffer.h"
#include "fs/vfs.h"

static int cmd_config_keyboard(struct shell_context *ctx, int argc,
                               char **argv) {
  const char *language = ctx && ctx->session ? session_language(ctx->session)
                                             : "pt-BR";
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
    shell_print(cur ? cur : "(desconhecido)");
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
    shell_print(cur ? cur : "(desconhecido)");
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
  const char *language = ctx && ctx->session ? session_language(ctx->session)
                                             : "pt-BR";
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
    shell_print(" - capyos : padrao verde/escuro do sistema\n");
    shell_print(" - ocean  : variante azul/ciano\n");
    shell_print(" - forest : variante verde/floresta\n");
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
  const char *language = ctx && ctx->session ? session_language(ctx->session)
                                             : "pt-BR";
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
  const char *language = ctx && ctx->session ? session_language(ctx->session)
                                             : "pt-BR";

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
}

/* Pequeno delay para I/O. */
static void io_wait(void) { outb(0x80, 0); }

static void do_hard_reboot(void) {
  sync_and_flush();
  shell_print("Reiniciando...\n");
  cli();

  /* Metodo 1: Reset via controlador de teclado 8042. */
  uint8_t good = 0x02;
  while (good & 0x02)
    good = inb(0x64);
  outb(0x64, 0xFE);
  io_wait();

  /* Metodo 2: Triple fault (carrega IDT nulo e dispara interrupcao). */
  struct {
    uint16_t limit;
    uint32_t base;
  } __attribute__((packed)) null_idt = {0, 0};
  __asm__ volatile("lidt %0" : : "m"(null_idt));
  __asm__ volatile("int $0x03");

  while (1) {
    hlt();
  }
}

static void do_power_off(void) {
  sync_and_flush();
  shell_print("Desligando...\n");
  cli();

  /* Metodo 1: ACPI shutdown (S5 state). */
  acpi_shutdown();

  /* Fallback: QEMU/Bochs debug exit ports. */
  outw(0x604, 0x2000);  /* QEMU isa-debug-exit (newer) */
  outw(0xB004, 0x2000); /* Bochs/older QEMU */

  /* Last resort: halt. */
  while (1) {
    hlt();
  }
}

static int cmd_shutdown_reboot(struct shell_context *ctx, int argc,
                               char **argv) {
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: shutdown-reboot\nReinicia o sistema de forma controlada "
                "(sincroniza buffers).");
    shell_newline();
    return 0;
  }
  do_hard_reboot();
  return 0;
}

static int cmd_shutdown_off(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: shutdown-off\nDesliga o sistema (halt) apos sincronizar "
                "buffers.");
    shell_newline();
    return 0;
  }
  do_power_off();
  return 0;
}

static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: do-sync\nForca a gravacao do buffer de disco.");
    shell_newline();
    return 0;
  }
  (void)argc;
  (void)argv;
  struct super_block *root = vfs_root();
  if (!root || !root->bdev) {
    shell_print_error("sem dispositivo");
    return -1;
  }
  if (buffer_cache_sync(root->bdev) != 0) {
    shell_print_error("falha ao sincronizar buffers de disco");
    return -1;
  }
  shell_print_ok("buffers sincronizados");
  return 0;
}

static struct shell_command g_system_control_commands[7];
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
  g_system_control_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_control(size_t *count) {
  init_system_control_commands();
  if (count) {
    *count = 7;
  }
  return g_system_control_commands;
}
