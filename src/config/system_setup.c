/* system_setup.c: normalizers, runtime platform, theme/keyboard/splash apply,
   login. */
#include "config_internal.h"

/* ---- globals ---- */
char g_boot_default_keyboard_layout[16] = "us";
char g_boot_default_language[16] = "en";
struct system_runtime_platform g_runtime_platform = {0};

/* ---- installer config (populated from handoff, consumed by first_boot) ---- */
static int g_installer_config_ready = 0;
static char g_installer_hostname[32] = {0};
static char g_installer_theme[16] = {0};
static char g_installer_admin_username[32] = {0};
static char g_installer_admin_password[64] = {0};
static int g_installer_splash_enabled = 1;

/* ---- buffer_append shared implementation ---- */
void config_buffer_append(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0 || !src) {
    return;
  }
  size_t idx = cstring_length(dst);
  size_t sidx = 0;
  while (src[sidx] && idx < dst_size - 1) {
    dst[idx++] = src[sidx++];
  }
  dst[idx] = '\0';
}

/* ---- normalizer functions ---- */
const char *normalize_keyboard_layout_name(const char *input) {
  if (!input || !input[0]) {
    return "us";
  }
  if (strings_equal(input, "br-abnt2")) {
    return "br-abnt2";
  }
  if (strings_equal(input, "us")) {
    return "us";
  }
  return "us";
}

const char *system_language_or_default(const char *language) {
  const char *normalized = localization_normalize_language(language);
  return normalized ? normalized : "en";
}

const char *system_network_mode_or_default(const char *mode) {
  if (mode && strings_equal(mode, "dhcp")) {
    return "dhcp";
  }
  return "static";
}

const char *system_update_channel_or_default(const char *channel) {
  if (channel && strings_equal(channel, "develop")) {
    return "develop";
  }
  return "stable";
}

const char *system_update_branch_for_channel(const char *channel) {
  return strings_equal(system_update_channel_or_default(channel), "develop")
             ? "develop"
             : "main";
}

const char *system_update_manifest_for_channel(const char *channel) {
  (void)channel;
  return "/system/update/latest.ini";
}

const char *system_service_target_or_default(const char *target) {
  struct system_service_target_status status;
  if (service_manager_target_find(target, &status) == 0) {
    return service_manager_target_label(status.id);
  }
  return service_manager_target_label(SYSTEM_SERVICE_TARGET_NETWORK);
}

static uint32_t system_ipv4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) |
         (uint32_t)d;
}

void system_ipv4_to_string(uint32_t ip, char out[16]) {
  char *p = out;
  uint8_t octets[4];
  if (!out) {
    return;
  }
  octets[0] = (uint8_t)((ip >> 24) & 0xFFu);
  octets[1] = (uint8_t)((ip >> 16) & 0xFFu);
  octets[2] = (uint8_t)((ip >> 8) & 0xFFu);
  octets[3] = (uint8_t)(ip & 0xFFu);
  for (int i = 0; i < 4; ++i) {
    uint8_t v = octets[i];
    if (v >= 100) {
      *p++ = (char)('0' + v / 100);
      *p++ = (char)('0' + (v / 10) % 10);
      *p++ = (char)('0' + v % 10);
    } else if (v >= 10) {
      *p++ = (char)('0' + v / 10);
      *p++ = (char)('0' + v % 10);
    } else {
      *p++ = (char)('0' + v);
    }
    if (i < 3) {
      *p++ = '.';
    }
  }
  *p = '\0';
}

int system_parse_ipv4(const char *text, uint32_t *out) {
  uint32_t parts[4] = {0};
  int part_idx = 0;
  if (!text || !out) {
    return -1;
  }
  for (const char *p = text; *p; ++p) {
    if (*p == '.') {
      if (++part_idx >= 4) {
        return -1;
      }
      continue;
    }
    if (*p < '0' || *p > '9') {
      return -1;
    }
    parts[part_idx] = parts[part_idx] * 10 + (uint32_t)(*p - '0');
    if (parts[part_idx] > 255) {
      return -1;
    }
  }
  if (part_idx != 3) {
    return -1;
  }
  *out = system_ipv4_addr((uint8_t)parts[0], (uint8_t)parts[1],
                          (uint8_t)parts[2], (uint8_t)parts[3]);
  return 0;
}

/* ---- boot defaults ---- */
void system_set_boot_defaults(const char *keyboard_layout,
                              const char *language) {
  cstring_copy(g_boot_default_keyboard_layout,
               sizeof(g_boot_default_keyboard_layout),
               normalize_keyboard_layout_name(keyboard_layout));
  cstring_copy(g_boot_default_language, sizeof(g_boot_default_language),
               system_language_or_default(language));
}

/* ---- installer config ---- */
void system_set_installer_config(const char *hostname, const char *theme,
                                 const char *admin_username,
                                 const char *admin_password,
                                 int splash_enabled) {
  if (!hostname || !hostname[0] || !admin_username || !admin_username[0] ||
      !admin_password || !admin_password[0]) {
    return;
  }
  cstring_copy(g_installer_hostname, sizeof(g_installer_hostname), hostname);
  cstring_copy(g_installer_theme, sizeof(g_installer_theme),
               (theme && theme[0]) ? theme : "capyos");
  cstring_copy(g_installer_admin_username, sizeof(g_installer_admin_username),
               admin_username);
  cstring_copy(g_installer_admin_password, sizeof(g_installer_admin_password),
               admin_password);
  g_installer_splash_enabled = splash_enabled;
  g_installer_config_ready = 1;
}

int system_installer_config_available(void) {
  return g_installer_config_ready;
}

const char *system_installer_hostname(void) {
  return g_installer_config_ready ? g_installer_hostname : NULL;
}

const char *system_installer_theme(void) {
  return g_installer_config_ready ? g_installer_theme : NULL;
}

const char *system_installer_admin_username(void) {
  return g_installer_config_ready ? g_installer_admin_username : NULL;
}

const char *system_installer_admin_password(void) {
  return g_installer_config_ready ? g_installer_admin_password : NULL;
}

int system_installer_splash_enabled(void) {
  return g_installer_config_ready ? g_installer_splash_enabled : -1;
}

void system_installer_clear_password(void) {
  memory_zero(g_installer_admin_password, sizeof(g_installer_admin_password));
}

/* ---- runtime platform ---- */
void system_runtime_platform_set(
    const struct system_runtime_platform *status) {
  if (!status) {
    memory_zero(&g_runtime_platform, sizeof(g_runtime_platform));
    return;
  }
  g_runtime_platform = *status;
}

void system_runtime_platform_get(struct system_runtime_platform *out) {
  if (!out) {
    return;
  }
  *out = g_runtime_platform;
}

/* ---- gate labels ---- */
const char *system_exit_boot_services_gate_label(uint8_t gate) {
  switch (gate) {
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_NATIVE:
    return "native";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_READY:
    return "ready";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_CONTRACT:
    return "wait-contract";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT:
    return "wait-input";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_DEVICE:
    return "wait-storage-device";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE:
    return "wait-storage-firmware";
  case SYSTEM_EXIT_BOOT_SERVICES_GATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

const char *system_hyperv_input_gate_label(uint8_t gate) {
  switch (gate) {
  case SYSTEM_HYPERV_INPUT_GATE_OFF:
    return "off";
  case SYSTEM_HYPERV_INPUT_GATE_ACTIVE:
    return "active";
  case SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES:
    return "wait-boot-services";
  case SYSTEM_HYPERV_INPUT_GATE_PREPARED:
    return "prepared";
  case SYSTEM_HYPERV_INPUT_GATE_READY:
    return "ready";
  case SYSTEM_HYPERV_INPUT_GATE_RETRY:
    return "retry";
  case SYSTEM_HYPERV_INPUT_GATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

/* ---- apply theme ---- */
void __attribute__((weak)) system_platform_apply_theme(const char *theme) {
  (void)theme;
}
void __attribute__((weak))
system_platform_sync_theme(const struct system_settings *settings) {
  (void)settings;
}

void system_apply_theme(const struct system_settings *settings) {
  const char *theme = settings ? settings->theme : NULL;
  if (!theme) {
    vga_set_color(7, 0);
    system_platform_apply_theme("capyos");
    system_platform_sync_theme(settings);
    return;
  }
  if (strings_equal(theme, "ocean")) {
    vga_set_color(11, 1);
  } else if (strings_equal(theme, "forest")) {
    vga_set_color(10, 2);
  } else {
    vga_set_color(7, 0);
    theme = "capyos";
  }
  system_platform_apply_theme(theme);
  system_platform_sync_theme(settings);
}

/* ---- apply keyboard ---- */
void system_apply_keyboard_layout(const struct system_settings *settings) {
  const char *layout = (settings && settings->keyboard_layout[0])
                           ? settings->keyboard_layout
                           : "us";
  if (keyboard_set_layout_by_name(layout) != 0) {
    config_log_event(
        "[kbd] layout desconhecido em config.ini; revertendo para 'us'.");
    keyboard_set_layout_by_name("us");
  }
}

/* ---- show splash ---- */
void system_show_splash(const struct system_settings *settings) {
  if (!settings || !settings->splash_enabled) {
    return;
  }
  static const char frames[][13] = {"[=         ]", "[===       ]",
                                    "[======    ]", "[========= ]",
                                    "[==========]"};
  for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
    vga_clear();
    vga_write("CapyOS iniciando...\n\n");
    vga_write(frames[i]);
    vga_write("\n");
    for (volatile uint32_t wait = 0; wait < 1000000; ++wait) {
      __asm__ volatile("");
    }
  }
  vga_clear();
}

/* ---- login ---- */
int system_login(struct session_context *session,
                 const struct system_settings *settings) {
  const char *language =
      settings ? system_language_or_default(settings->language) : "en";
  if (!session) {
    return -1;
  }
  while (system_detect_first_boot() != 0) {
    char retry_setup[8];
    config_print_line(localization_select(
        language,
        "A configuracao inicial precisa ser concluida antes do login.",
        "Initial setup must be completed before login.",
        "La configuracion inicial debe completarse antes del inicio de sesion."));
    if (system_run_first_boot_setup() == 0 &&
        system_detect_first_boot() == 0) {
      break;
    }
    config_print_line(localization_select(
        language,
        "O assistente nao concluiu.",
        "The wizard did not complete.",
        "El asistente no termino."));
    memory_zero(retry_setup, sizeof(retry_setup));
    (void)wizard_prompt(localization_select(
                            language,
                            "Pressione ENTER para tentar novamente: ",
                            "Press ENTER to try again: ",
                            "Presiona ENTER para intentar nuevamente: "),
                        retry_setup, sizeof(retry_setup), 0);
  }
  const char *proc_login = "autenticacao de usuario";
  config_log_process_begin(proc_login);
  config_log_process_begin_success(proc_login);
  vga_newline();
  config_print_line(system_ui_text(language, SYS_UI_LOGIN_TITLE));
  if (settings) {
    char host_msg[128];
    host_msg[0] = '\0';
    config_buffer_append(host_msg, sizeof(host_msg),
                         system_ui_text(language, SYS_UI_LOGIN_HOST_PREFIX));
    config_buffer_append(host_msg, sizeof(host_msg), settings->hostname);
    config_print_line(host_msg);
  }
  char username[USER_NAME_MAX];
  char password[TTY_BUFFER_MAX];
  struct user_record record;

  while (1) {
    memory_zero(username, sizeof(username));
    memory_zero(password, sizeof(password));
    config_log_process_progress(proc_login);
    size_t ulen = wizard_prompt(
        system_ui_text(language, SYS_UI_LOGIN_USERNAME_PROMPT), username,
        sizeof(username), 0);
    size_t plen = wizard_prompt(
        system_ui_text(language, SYS_UI_LOGIN_PASSWORD_PROMPT), password,
        sizeof(password), 1);
    if (ulen == 0 || plen == 0) {
      config_print_line(
          system_ui_text(language, SYS_UI_LOGIN_CREDENTIALS_REQUIRED));
      continue;
    }
    char attempt_msg[160];
    attempt_msg[0] = '\0';
    config_buffer_append(attempt_msg, sizeof(attempt_msg),
                         "Login tentativa para usuario=");
    config_buffer_append(attempt_msg, sizeof(attempt_msg), username);
    config_log_event(attempt_msg);

    if (userdb_authenticate(username, password, &record) == 0) {
      memory_zero(password, sizeof(password));
      session_begin(session, &record, language);
      char welcome[160];
      language = session_language(session);
      welcome[0] = '\0';
      config_buffer_append(
          welcome, sizeof(welcome),
          localization_text_for(language, LOC_TEXT_WELCOME_PREFIX));
      config_buffer_append(welcome, sizeof(welcome), record.username);
      config_buffer_append(welcome, sizeof(welcome), ".");
      config_print_line(welcome);
      vga_newline();
      char success_msg[160];
      success_msg[0] = '\0';
      config_buffer_append(success_msg, sizeof(success_msg),
                           "Login bem-sucedido: usuario=");
      config_buffer_append(success_msg, sizeof(success_msg), record.username);
      config_log_event(success_msg);
      config_log_process_conclude(proc_login);
      config_log_process_finalize(proc_login);
      config_log_process_finalize_success(proc_login);
      return 0;
    }
    memory_zero(password, sizeof(password));
    config_print_line(system_ui_text(language, SYS_UI_LOGIN_INVALID));
    char fail_msg[160];
    fail_msg[0] = '\0';
    config_buffer_append(fail_msg, sizeof(fail_msg),
                         "Login falhou: usuario=");
    config_buffer_append(fail_msg, sizeof(fail_msg), username);
    config_log_event(fail_msg);
  }
}
