/* system_setup_wizard.c: TUI wizard menus, UI text translations, prompts. */
#include "internal/config_internal.h"

#if defined(__x86_64__)
#include "drivers/serial/com1.h"
#endif

static void wizard_serial_write(const char *text) {
#if defined(__x86_64__)
  if (text) {
    com1_puts(text);
  }
#else
  (void)text;
#endif
}

/* ---- menu helpers ---- */
static const char *system_ui_menu_hint(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Use Up/Down, number keys, and Enter.";
  }
  if (strings_equal(normalized, "es")) {
    return "Usa flechas, numeros y Enter.";
  }
  return "Use setas, numeros e Enter.";
}

const char *system_ui_menu_enabled(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Enabled";
  }
  if (strings_equal(normalized, "es")) {
    return "Activado";
  }
  return "Ativado";
}

const char *system_ui_menu_disabled(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Disabled";
  }
  if (strings_equal(normalized, "es")) {
    return "Desactivado";
  }
  return "Desativado";
}

const char *system_ui_splash_menu_title(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Animated splash";
  }
  if (strings_equal(normalized, "es")) {
    return "Splash animado";
  }
  return "Splash animado";
}

/* ---- draw helpers ---- */
void wizard_draw_header(uint32_t progress, const char *title) {
  char pct[8];
  vga_clear();
  vga_write("CAPYOS SETUP");
  vga_newline();
  vga_newline();
  pct[0] = '\0';
  config_u32_to_string(progress, pct, sizeof(pct));
  vga_write("[");
  vga_write(pct);
  vga_write("%]");
  if (title && title[0]) {
    vga_write(" ");
    vga_write(title);
  }
  vga_newline();
  vga_newline();
}

static void wizard_draw_menu(const char *title, const char *language,
                             const char *const *items, size_t count,
                             size_t selected) {
  vga_clear();
  vga_write("CAPYOS SETUP");
  vga_newline();
  vga_newline();
  if (title && title[0]) {
    vga_write(title);
    vga_newline();
  }
  vga_write(system_ui_menu_hint(language));
  vga_newline();
  vga_newline();

  for (size_t i = 0; i < count; ++i) {
    char line[160];
    char idx[12];
    line[0] = '\0';
    config_u32_to_string((uint32_t)(i + 1u), idx, sizeof(idx));
    config_buffer_append(line, sizeof(line), (i == selected) ? "> " : "  ");
    config_buffer_append(line, sizeof(line), "[");
    config_buffer_append(line, sizeof(line), idx);
    config_buffer_append(line, sizeof(line), "] ");
    config_buffer_append(line, sizeof(line), items[i] ? items[i] : "");
    vga_write(line);
    vga_newline();
  }
}

static int wizard_menu_select(const char *title, const char *language,
                              const char *const *items, size_t count,
                              size_t default_index) {
  size_t selected = 0;
  if (!items || count == 0) {
    return 0;
  }
  if (default_index < count) {
    selected = default_index;
  }
  tty_set_echo(1);
  tty_set_echo_mask('\0');
  for (;;) {
    char ch;
    wizard_draw_menu(title, language, items, count, selected);
    ch = tty_getc();
    if (ch == '\r' || ch == '\n') {
      return (int)selected;
    }
    if (ch >= '1' && ch <= '9') {
      size_t pick = (size_t)(ch - '1');
      if (pick < count) {
        return (int)pick;
      }
      continue;
    }
    if (ch != 27) {
      continue;
    }
    ch = tty_getc();
    if (ch != '[') {
      continue;
    }
    ch = tty_getc();
    if (ch == 'A') {
      selected = (selected == 0) ? (count - 1) : (selected - 1);
    } else if (ch == 'B') {
      selected = (selected + 1u) % count;
    }
  }
}

/* ---- public prompt functions ---- */
size_t wizard_prompt(const char *prompt, char *buffer, size_t buffer_len,
                     int secret) {
  if (!buffer || buffer_len == 0) {
    return 0;
  }
  if (secret) {
    tty_set_echo_mask('*');
  } else {
    tty_set_echo(1);
    tty_set_echo_mask('\0');
  }
  wizard_serial_write(prompt ? prompt : "");
  tty_set_prompt(prompt ? prompt : "");
  tty_show_prompt();
  size_t len = tty_readline(buffer, buffer_len);
  wizard_serial_write("\r\n");
  tty_set_echo(1);
  tty_set_echo_mask('\0');
  return len;
}

size_t wizard_prompt_setup(uint32_t progress, const char *title,
                           const char *prompt, char *buffer,
                           size_t buffer_len, int secret) {
  wizard_draw_header(progress, title);
  return wizard_prompt(prompt, buffer, buffer_len, secret);
}

int wizard_menu_select_setup(uint32_t progress, const char *title,
                             const char *language,
                             const char *const *items, size_t count,
                             size_t default_index) {
  char menu_title[160];
  char pct[8];
  menu_title[0] = '\0';
  config_u32_to_string(progress, pct, sizeof(pct));
  config_buffer_append(menu_title, sizeof(menu_title), "[");
  config_buffer_append(menu_title, sizeof(menu_title), pct);
  config_buffer_append(menu_title, sizeof(menu_title), "%] ");
  config_buffer_append(menu_title, sizeof(menu_title), title ? title : "");
  return wizard_menu_select(menu_title, language, items, count, default_index);
}

int prompt_password_pair(const char *label, char *password,
                         size_t password_len, const char *language) {
  for (int tries = 0; tries < 3; ++tries) {
    char confirm[TTY_BUFFER_MAX];
    memory_zero(password, password_len);
    memory_zero(confirm, sizeof(confirm));
    size_t plen = wizard_prompt(label, password, password_len, 1);
    size_t clen = wizard_prompt(
        system_ui_text(language, SYS_UI_PASSWORD_CONFIRM_PROMPT), confirm,
        sizeof(confirm), 1);
    if (plen == 0 || clen == 0) {
      config_print_line(system_ui_text(language, SYS_UI_PASSWORD_EMPTY));
      continue;
    }
    if (plen != clen) {
      config_print_line(system_ui_text(language, SYS_UI_PASSWORD_MISMATCH));
      continue;
    }
    int match = 1;
    for (size_t i = 0; i < plen; ++i) {
      if (password[i] != confirm[i]) {
        match = 0;
        break;
      }
    }
    memory_zero(confirm, sizeof(confirm));
    if (!match) {
      config_print_line(system_ui_text(language, SYS_UI_PASSWORD_MISMATCH));
      continue;
    }
    return 0;
  }
  return -1;
}

/* ---- UI text translations ---- */
const char *system_ui_text(const char *language, enum system_ui_text_id id) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    switch (id) {
    case SYS_UI_PASSWORD_CONFIRM_PROMPT:
      return "Confirm password: ";
    case SYS_UI_PASSWORD_EMPTY:
      return "Password cannot be empty.";
    case SYS_UI_PASSWORD_MISMATCH:
      return "Passwords do not match.";
    case SYS_UI_LAYOUTS_AVAILABLE:
      return "Available keyboard layouts:";
    case SYS_UI_LAYOUT_PROMPT:
      return "Keyboard layout [us]: ";
    case SYS_UI_LAYOUT_APPLIED_PREFIX:
      return "   Layout applied: ";
    case SYS_UI_LAYOUT_UNKNOWN:
      return "Unknown layout. Choose a valid index or name.";
    case SYS_UI_HOSTNAME_PROMPT:
      return "Hostname [capyos-node]: ";
    case SYS_UI_HOSTNAME_DEFINED_PREFIX:
      return "   Hostname set: ";
    case SYS_UI_THEMES_AVAILABLE:
      return "Available themes: capyos, ocean, forest.";
    case SYS_UI_THEME_PROMPT:
      return "Theme [capyos]: ";
    case SYS_UI_THEME_SELECTED_PREFIX:
      return "   Selected theme: ";
    case SYS_UI_SPLASH_PROMPT:
      return "Enable animated splash? [Y/n]: ";
    case SYS_UI_SPLASH_ENABLED:
      return "   Animated splash: enabled";
    case SYS_UI_SPLASH_DISABLED:
      return "   Animated splash: disabled";
    case SYS_UI_ADMIN_USER_PROMPT:
      return "Administrator user [admin]: ";
    case SYS_UI_ADMIN_USER_INVALID:
      return "Invalid username; using default 'admin'.";
    case SYS_UI_ADMIN_HOME_CREATE_FAIL:
      return "Failed to create the administrator home directory.";
    case SYS_UI_ADMIN_HOME_UNAVAILABLE:
      return "Administrator home directory unavailable.";
    case SYS_UI_ADMIN_HOME_PERM_WARNING:
      return "Warning: could not adjust administrator home permissions.";
    case SYS_UI_ADMIN_EXISTS:
      return "   Administrator user already exists. Validating current record.";
    case SYS_UI_ADMIN_HOME_REBUILD_PREFIX:
      return "   Home directory missing. Recreating in ";
    case SYS_UI_ADMIN_HOME_REBUILD_FAIL:
      return "   Failed to rebuild the administrator home directory.";
    case SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX:
      return "Set the password for user ";
    case SYS_UI_ADMIN_REGISTER_FAIL:
      return "Could not register the administrator user.";
    case SYS_UI_ADMIN_RECORD_BUILD_FAIL:
      return "Failed to build the administrator record.";
    case SYS_UI_ADMIN_SAVE_FAIL:
      return "Failed to save the administrator user.";
    case SYS_UI_ADMIN_AUTH_REBUILD_FAIL:
      return "Administrator authentication test failed. Rebuilding user database.";
    case SYS_UI_ADMIN_USERDB_REBUILD_FAIL:
      return "Could not rebuild /etc/users.db.";
    case SYS_UI_ADMIN_VALIDATED:
      return "   Administrator user validated successfully.";
    case SYS_UI_CONFIG_WRITE_FAIL:
      return "Failed to write initial configuration.";
    case SYS_UI_CONFIG_VALIDATED:
      return "   /system/config.ini validated successfully.";
    case SYS_UI_FIRST_BOOT_COMPLETE_FAIL:
      return "Could not mark setup completion.";
    case SYS_UI_LOGIN_TITLE:
      return "== CapyOS Login ==";
    case SYS_UI_LOGIN_HOST_PREFIX:
      return "Host: ";
    case SYS_UI_LOGIN_USERNAME_PROMPT:
      return "User: ";
    case SYS_UI_LOGIN_PASSWORD_PROMPT:
      return "Password: ";
    case SYS_UI_LOGIN_CREDENTIALS_REQUIRED:
      return "Credentials required.";
    case SYS_UI_LOGIN_INVALID:
      return "Invalid username or password.";
    default:
      return "";
    }
  }
  if (strings_equal(normalized, "es")) {
    switch (id) {
    case SYS_UI_PASSWORD_CONFIRM_PROMPT:
      return "Confirma la contrasena: ";
    case SYS_UI_PASSWORD_EMPTY:
      return "La contrasena no puede estar vacia.";
    case SYS_UI_PASSWORD_MISMATCH:
      return "Las contrasenas no coinciden.";
    case SYS_UI_LAYOUTS_AVAILABLE:
      return "Layouts de teclado disponibles:";
    case SYS_UI_LAYOUT_PROMPT:
      return "Layout del teclado [us]: ";
    case SYS_UI_LAYOUT_APPLIED_PREFIX:
      return "   Layout aplicado: ";
    case SYS_UI_LAYOUT_UNKNOWN:
      return "Layout desconocido. Elige un indice o nombre valido.";
    case SYS_UI_HOSTNAME_PROMPT:
      return "Hostname [capyos-node]: ";
    case SYS_UI_HOSTNAME_DEFINED_PREFIX:
      return "   Hostname definido: ";
    case SYS_UI_THEMES_AVAILABLE:
      return "Temas disponibles: capyos, ocean, forest.";
    case SYS_UI_THEME_PROMPT:
      return "Tema [capyos]: ";
    case SYS_UI_THEME_SELECTED_PREFIX:
      return "   Tema seleccionado: ";
    case SYS_UI_SPLASH_PROMPT:
      return "Activar splash animado? [S/n]: ";
    case SYS_UI_SPLASH_ENABLED:
      return "   Splash animado: habilitado";
    case SYS_UI_SPLASH_DISABLED:
      return "   Splash animado: deshabilitado";
    case SYS_UI_ADMIN_USER_PROMPT:
      return "Usuario administrador [admin]: ";
    case SYS_UI_ADMIN_USER_INVALID:
      return "Nombre de usuario invalido; usando 'admin'.";
    case SYS_UI_ADMIN_HOME_CREATE_FAIL:
      return "Fallo al crear el directorio home del administrador.";
    case SYS_UI_ADMIN_HOME_UNAVAILABLE:
      return "Directorio home del administrador no disponible.";
    case SYS_UI_ADMIN_HOME_PERM_WARNING:
      return "Aviso: no fue posible ajustar los permisos del home.";
    case SYS_UI_ADMIN_EXISTS:
      return "   El usuario administrador ya existe. Validando registro actual.";
    case SYS_UI_ADMIN_HOME_REBUILD_PREFIX:
      return "   Home ausente. Recreando en ";
    case SYS_UI_ADMIN_HOME_REBUILD_FAIL:
      return "   Fallo al reconstruir el home del administrador.";
    case SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX:
      return "Define la contrasena para el usuario ";
    case SYS_UI_ADMIN_REGISTER_FAIL:
      return "No fue posible registrar el usuario administrador.";
    case SYS_UI_ADMIN_RECORD_BUILD_FAIL:
      return "Error al montar el registro del administrador.";
    case SYS_UI_ADMIN_SAVE_FAIL:
      return "Error al guardar el usuario administrador.";
    case SYS_UI_ADMIN_AUTH_REBUILD_FAIL:
      return "La validacion del administrador fallo. Reconstruyendo la base de usuarios.";
    case SYS_UI_ADMIN_USERDB_REBUILD_FAIL:
      return "No fue posible reconstruir /etc/users.db.";
    case SYS_UI_ADMIN_VALIDATED:
      return "   Usuario administrador validado correctamente.";
    case SYS_UI_CONFIG_WRITE_FAIL:
      return "Fallo al grabar la configuracion inicial.";
    case SYS_UI_CONFIG_VALIDATED:
      return "   /system/config.ini validado correctamente.";
    case SYS_UI_FIRST_BOOT_COMPLETE_FAIL:
      return "No fue posible registrar el fin de la configuracion.";
    case SYS_UI_LOGIN_TITLE:
      return "== Inicio de sesion CapyOS ==";
    case SYS_UI_LOGIN_HOST_PREFIX:
      return "Host: ";
    case SYS_UI_LOGIN_USERNAME_PROMPT:
      return "Usuario: ";
    case SYS_UI_LOGIN_PASSWORD_PROMPT:
      return "Contrasena: ";
    case SYS_UI_LOGIN_CREDENTIALS_REQUIRED:
      return "Credenciales obligatorias.";
    case SYS_UI_LOGIN_INVALID:
      return "Usuario o contrasena invalidos.";
    default:
      return "";
    }
  }

  /* Portuguese (default) */
  switch (id) {
  case SYS_UI_PASSWORD_CONFIRM_PROMPT:
    return "Confirme a senha: ";
  case SYS_UI_PASSWORD_EMPTY:
    return "Senha nao pode ser vazia.";
  case SYS_UI_PASSWORD_MISMATCH:
    return "As senhas nao coincidem.";
  case SYS_UI_LAYOUTS_AVAILABLE:
    return "Layouts de teclado disponiveis:";
  case SYS_UI_LAYOUT_PROMPT:
    return "Layout do teclado [us]: ";
  case SYS_UI_LAYOUT_APPLIED_PREFIX:
    return "   Layout aplicado: ";
  case SYS_UI_LAYOUT_UNKNOWN:
    return "Layout desconhecido. Escolha um indice ou nome valido.";
  case SYS_UI_HOSTNAME_PROMPT:
    return "Hostname [capyos-node]: ";
  case SYS_UI_HOSTNAME_DEFINED_PREFIX:
    return "   Hostname definido: ";
  case SYS_UI_THEMES_AVAILABLE:
    return "Temas disponiveis: capyos, ocean, forest.";
  case SYS_UI_THEME_PROMPT:
    return "Tema [capyos]: ";
  case SYS_UI_THEME_SELECTED_PREFIX:
    return "   Tema selecionado: ";
  case SYS_UI_SPLASH_PROMPT:
    return "Ativar splash animado? [S/n]: ";
  case SYS_UI_SPLASH_ENABLED:
    return "   Splash animado: habilitado";
  case SYS_UI_SPLASH_DISABLED:
    return "   Splash animado: desabilitado";
  case SYS_UI_ADMIN_USER_PROMPT:
    return "Usuario administrador [admin]: ";
  case SYS_UI_ADMIN_USER_INVALID:
    return "Nome de usuario invalido; usando padrao 'admin'.";
  case SYS_UI_ADMIN_HOME_CREATE_FAIL:
    return "Falha ao criar diretorio pessoal do administrador.";
  case SYS_UI_ADMIN_HOME_UNAVAILABLE:
    return "Diretorio pessoal do administrador indisponivel.";
  case SYS_UI_ADMIN_HOME_PERM_WARNING:
    return "Aviso: nao foi possivel ajustar permissoes do diretorio pessoal.";
  case SYS_UI_ADMIN_EXISTS:
    return "   Usuario administrador ja existente. Validando registro atual.";
  case SYS_UI_ADMIN_HOME_REBUILD_PREFIX:
    return "   Diretorio pessoal ausente. Recriando em ";
  case SYS_UI_ADMIN_HOME_REBUILD_FAIL:
    return "   Falha ao reconstruir diretorio pessoal do administrador.";
  case SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX:
    return "Defina a senha para o usuario ";
  case SYS_UI_ADMIN_REGISTER_FAIL:
    return "Nao foi possivel registrar o usuario administrador.";
  case SYS_UI_ADMIN_RECORD_BUILD_FAIL:
    return "Erro ao montar registro do administrador.";
  case SYS_UI_ADMIN_SAVE_FAIL:
    return "Erro ao salvar usuario administrador.";
  case SYS_UI_ADMIN_AUTH_REBUILD_FAIL:
    return "Falha no teste de autenticacao do administrador. Recriando base de usuarios.";
  case SYS_UI_ADMIN_USERDB_REBUILD_FAIL:
    return "Nao foi possivel reconstruir /etc/users.db.";
  case SYS_UI_ADMIN_VALIDATED:
    return "   Usuario administrador validado com sucesso.";
  case SYS_UI_CONFIG_WRITE_FAIL:
    return "Falha ao gravar configuracao inicial.";
  case SYS_UI_CONFIG_VALIDATED:
    return "   /system/config.ini validado com sucesso.";
  case SYS_UI_FIRST_BOOT_COMPLETE_FAIL:
    return "Nao foi possivel registrar conclusao da configuracao.";
  case SYS_UI_LOGIN_TITLE:
    return "== CapyOS Login ==";
  case SYS_UI_LOGIN_HOST_PREFIX:
    return "Host: ";
  case SYS_UI_LOGIN_USERNAME_PROMPT:
    return "Usuario: ";
  case SYS_UI_LOGIN_PASSWORD_PROMPT:
    return "Senha: ";
  case SYS_UI_LOGIN_CREDENTIALS_REQUIRED:
    return "Credenciais obrigatorias.";
  case SYS_UI_LOGIN_INVALID:
    return "Usuario ou senha invalidos.";
  default:
    return "";
  }
}
