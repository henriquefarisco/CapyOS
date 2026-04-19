#include "lang/localization.h"

#include <stddef.h>

typedef enum localization_language_index {
  LANG_PT_BR = 0,
  LANG_EN = 1,
  LANG_ES = 2,
} localization_language_index;

static int char_tolower_ascii(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A' + 'a';
  }
  return c;
}

static int strings_equal_ci(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (char_tolower_ascii(a[i]) != char_tolower_ascii(b[i])) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

static void normalize_language_token(const char *input, char *out,
                                     size_t out_size) {
  size_t start = 0;
  size_t end = 0;
  size_t pos = 0;
  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!input) {
    return;
  }
  while (input[start] && (unsigned char)input[start] <= 0x20u) {
    ++start;
  }
  end = start;
  while (input[end]) {
    ++end;
  }
  while (end > start && (unsigned char)input[end - 1] <= 0x20u) {
    --end;
  }
  while (start < end && pos + 1 < out_size) {
    char c = (char)char_tolower_ascii(input[start++]);
    out[pos++] = (c == '_') ? '-' : c;
  }
  out[pos] = '\0';
}

const char *localization_normalize_language(const char *input) {
  char token[16];
  normalize_language_token(input, token, sizeof(token));
  if (!token[0]) {
    return "pt-BR";
  }
  if (strings_equal_ci(token, "pt") || strings_equal_ci(token, "pt-br")) {
    return "pt-BR";
  }
  if (strings_equal_ci(token, "en") || strings_equal_ci(token, "en-us")) {
    return "en";
  }
  if (strings_equal_ci(token, "es") || strings_equal_ci(token, "es-es")) {
    return "es";
  }
  return NULL;
}

int localization_language_supported(const char *input) {
  return localization_normalize_language(input) != NULL;
}

static localization_language_index localization_index_for(const char *language) {
  const char *normalized = localization_normalize_language(language);
  if (!normalized || normalized[0] == 'p') {
    return LANG_PT_BR;
  }
  if (normalized[0] == 'e' && normalized[1] == 'n') {
    return LANG_EN;
  }
  return LANG_ES;
}

const char *localization_language_label(const char *language) {
  const char *normalized = localization_normalize_language(language);
  return normalized ? normalized : "pt-BR";
}

const char *localization_language_name(const char *language) {
  switch (localization_index_for(language)) {
  case LANG_EN:
    return "English";
  case LANG_ES:
    return "Espanol";
  case LANG_PT_BR:
  default:
    return "Portugues (Brasil)";
  }
}

const char *localization_select(const char *language, const char *pt_br,
                                const char *en, const char *es) {
  switch (localization_index_for(language)) {
  case LANG_EN:
    return en ? en : (pt_br ? pt_br : "");
  case LANG_ES:
    return es ? es : (pt_br ? pt_br : "");
  case LANG_PT_BR:
  default:
    return pt_br ? pt_br : "";
  }
}

const char *localization_text_for(const char *language,
                                  enum localization_text_id id) {
  switch (localization_index_for(language)) {
  case LANG_EN:
    switch (id) {
    case LOC_TEXT_COMMANDS_AVAILABLE:
      return "Available commands:\n";
    case LOC_TEXT_NO_COMMANDS_REGISTERED:
      return "No commands registered.\n";
    case LOC_TEXT_HELP_DOCS_HINT:
      return "Use help-docs for a quick reference.\n";
    case LOC_TEXT_DOCS_NOT_FOUND:
      return "Documentation not found in /docs/capyos-cli-reference.txt\n";
    case LOC_TEXT_DOCS_VERIFY_INSTALL:
      return "Check the installation or use help-any.\n";
    case LOC_TEXT_LOGOUT_MESSAGE:
      return "Logging out...\n";
    case LOC_TEXT_UNKNOWN_COMMAND_PREFIX:
      return "Unknown command: ";
    case LOC_TEXT_UNKNOWN_COMMAND_HINT:
      return "\nUse 'help-any' to list commands.\n";
    case LOC_TEXT_NO_INPUT_DEVICE:
      return "[!] No input device available.\n\n";
    case LOC_TEXT_PREPARE_SHELL_FAILED:
      return "[error] Failed to prepare shell runtime.\n";
    case LOC_TEXT_AUTH_FLOW_FAILED:
      return "[error] Authentication flow failed.\n";
    case LOC_TEXT_SESSION_ACTIVATION_FAILED:
      return "[error] Failed to activate authenticated session.\n";
    case LOC_TEXT_RETURNING_TO_LOGIN:
      return "Returning to the login screen.\n";
    case LOC_TEXT_WELCOME_PREFIX:
      return "Welcome, ";
    case LOC_TEXT_LAYOUT_CURRENT:
      return "Current keyboard layout: ";
    case LOC_TEXT_LAYOUT_LIST_HEADER:
      return "Available keyboard layouts:\n";
    case LOC_TEXT_LAYOUT_UNKNOWN:
      return "unknown keyboard layout";
    case LOC_TEXT_LAYOUT_UPDATED:
      return "keyboard layout updated";
    case LOC_TEXT_LAYOUT_UPDATED_SAVED:
      return "keyboard layout updated and saved";
    case LOC_TEXT_THEME_CURRENT:
      return "Current theme: ";
    case LOC_TEXT_THEME_LIST_HEADER:
      return "Available themes:\n";
    case LOC_TEXT_THEME_UNKNOWN:
      return "unknown theme";
    case LOC_TEXT_THEME_UPDATED:
      return "theme updated";
    case LOC_TEXT_THEME_UPDATED_SAVED:
      return "theme updated and saved";
    case LOC_TEXT_CONFIG_SAVE_WARNING:
      return "Warning: could not save to /system/config.ini.\n";
    case LOC_TEXT_SPLASH_CURRENT:
      return "Current splash: ";
    case LOC_TEXT_SPLASH_UNKNOWN:
      return "unknown splash mode";
    case LOC_TEXT_SPLASH_UPDATED:
      return "splash state updated";
    case LOC_TEXT_SPLASH_UPDATED_ON:
      return "splash enabled for the next boot";
    case LOC_TEXT_SPLASH_UPDATED_OFF:
      return "splash disabled; boot logs will be shown on the next boot";
    case LOC_TEXT_LANGUAGE_CURRENT:
      return "Current language: ";
    case LOC_TEXT_LANGUAGE_LIST_HEADER:
      return "Available languages:\n";
    case LOC_TEXT_LANGUAGE_UNKNOWN:
      return "unknown language";
    case LOC_TEXT_LANGUAGE_UPDATED:
      return "language updated";
    case LOC_TEXT_LANGUAGE_UPDATED_SAVED:
      return "language updated and saved";
    default:
      return "";
    }
  case LANG_ES:
    switch (id) {
    case LOC_TEXT_COMMANDS_AVAILABLE:
      return "Comandos disponibles:\n";
    case LOC_TEXT_NO_COMMANDS_REGISTERED:
      return "No hay comandos registrados.\n";
    case LOC_TEXT_HELP_DOCS_HINT:
      return "Usa help-docs para un resumen rapido.\n";
    case LOC_TEXT_DOCS_NOT_FOUND:
      return "Documentacion no encontrada en /docs/capyos-cli-reference.txt\n";
    case LOC_TEXT_DOCS_VERIFY_INSTALL:
      return "Verifica la instalacion o usa help-any.\n";
    case LOC_TEXT_LOGOUT_MESSAGE:
      return "Cerrando sesion...\n";
    case LOC_TEXT_UNKNOWN_COMMAND_PREFIX:
      return "Comando desconocido: ";
    case LOC_TEXT_UNKNOWN_COMMAND_HINT:
      return "\nUsa 'help-any' para listar comandos.\n";
    case LOC_TEXT_NO_INPUT_DEVICE:
      return "[!] No hay dispositivo de entrada disponible.\n\n";
    case LOC_TEXT_PREPARE_SHELL_FAILED:
      return "[error] No fue posible preparar el runtime del shell.\n";
    case LOC_TEXT_AUTH_FLOW_FAILED:
      return "[error] El flujo de autenticacion fallo.\n";
    case LOC_TEXT_SESSION_ACTIVATION_FAILED:
      return "[error] No fue posible activar la sesion autenticada.\n";
    case LOC_TEXT_RETURNING_TO_LOGIN:
      return "Volviendo a la pantalla de inicio de sesion.\n";
    case LOC_TEXT_WELCOME_PREFIX:
      return "Bienvenido, ";
    case LOC_TEXT_LAYOUT_CURRENT:
      return "Layout actual del teclado: ";
    case LOC_TEXT_LAYOUT_LIST_HEADER:
      return "Layouts de teclado disponibles:\n";
    case LOC_TEXT_LAYOUT_UNKNOWN:
      return "layout de teclado desconocido";
    case LOC_TEXT_LAYOUT_UPDATED:
      return "layout de teclado actualizado";
    case LOC_TEXT_LAYOUT_UPDATED_SAVED:
      return "layout de teclado actualizado y guardado";
    case LOC_TEXT_THEME_CURRENT:
      return "Tema actual: ";
    case LOC_TEXT_THEME_LIST_HEADER:
      return "Temas disponibles:\n";
    case LOC_TEXT_THEME_UNKNOWN:
      return "tema desconocido";
    case LOC_TEXT_THEME_UPDATED:
      return "tema actualizado";
    case LOC_TEXT_THEME_UPDATED_SAVED:
      return "tema actualizado y guardado";
    case LOC_TEXT_CONFIG_SAVE_WARNING:
      return "Aviso: no fue posible guardar en /system/config.ini.\n";
    case LOC_TEXT_SPLASH_CURRENT:
      return "Splash actual: ";
    case LOC_TEXT_SPLASH_UNKNOWN:
      return "modo de splash desconocido";
    case LOC_TEXT_SPLASH_UPDATED:
      return "estado del splash actualizado";
    case LOC_TEXT_SPLASH_UPDATED_ON:
      return "splash habilitado para el proximo arranque";
    case LOC_TEXT_SPLASH_UPDATED_OFF:
      return "splash deshabilitado; los logs del arranque se mostraran en el proximo inicio";
    case LOC_TEXT_LANGUAGE_CURRENT:
      return "Idioma actual: ";
    case LOC_TEXT_LANGUAGE_LIST_HEADER:
      return "Idiomas disponibles:\n";
    case LOC_TEXT_LANGUAGE_UNKNOWN:
      return "idioma desconocido";
    case LOC_TEXT_LANGUAGE_UPDATED:
      return "idioma actualizado";
    case LOC_TEXT_LANGUAGE_UPDATED_SAVED:
      return "idioma actualizado y guardado";
    default:
      return "";
    }
  case LANG_PT_BR:
  default:
    switch (id) {
    case LOC_TEXT_COMMANDS_AVAILABLE:
      return "Comandos disponiveis:\n";
    case LOC_TEXT_NO_COMMANDS_REGISTERED:
      return "Nenhum comando registrado.\n";
    case LOC_TEXT_HELP_DOCS_HINT:
      return "Use help-docs para resumo rapido.\n";
    case LOC_TEXT_DOCS_NOT_FOUND:
      return "Documentacao nao encontrada em /docs/capyos-cli-reference.txt\n";
    case LOC_TEXT_DOCS_VERIFY_INSTALL:
      return "Verifique instalacao ou use help-any.\n";
    case LOC_TEXT_LOGOUT_MESSAGE:
      return "Encerrando sessao...\n";
    case LOC_TEXT_UNKNOWN_COMMAND_PREFIX:
      return "Comando desconhecido: ";
    case LOC_TEXT_UNKNOWN_COMMAND_HINT:
      return "\nUse 'help-any' para listar comandos.\n";
    case LOC_TEXT_NO_INPUT_DEVICE:
      return "[!] Sem dispositivo de entrada disponivel.\n\n";
    case LOC_TEXT_PREPARE_SHELL_FAILED:
      return "[erro] Falha ao preparar runtime do shell.\n";
    case LOC_TEXT_AUTH_FLOW_FAILED:
      return "[erro] Falha no fluxo de autenticacao.\n";
    case LOC_TEXT_SESSION_ACTIVATION_FAILED:
      return "[erro] Falha ao ativar sessao autenticada.\n";
    case LOC_TEXT_RETURNING_TO_LOGIN:
      return "Retornando para a tela de login.\n";
    case LOC_TEXT_WELCOME_PREFIX:
      return "Bem-vindo, ";
    case LOC_TEXT_LAYOUT_CURRENT:
      return "Layout atual: ";
    case LOC_TEXT_LAYOUT_LIST_HEADER:
      return "Layouts disponiveis:\n";
    case LOC_TEXT_LAYOUT_UNKNOWN:
      return "layout desconhecido";
    case LOC_TEXT_LAYOUT_UPDATED:
      return "layout atualizado";
    case LOC_TEXT_LAYOUT_UPDATED_SAVED:
      return "layout atualizado e salvo";
    case LOC_TEXT_THEME_CURRENT:
      return "Tema atual: ";
    case LOC_TEXT_THEME_LIST_HEADER:
      return "Temas disponiveis:\n";
    case LOC_TEXT_THEME_UNKNOWN:
      return "tema desconhecido";
    case LOC_TEXT_THEME_UPDATED:
      return "tema atualizado";
    case LOC_TEXT_THEME_UPDATED_SAVED:
      return "tema atualizado e salvo";
    case LOC_TEXT_CONFIG_SAVE_WARNING:
      return "Aviso: nao foi possivel salvar em /system/config.ini.\n";
    case LOC_TEXT_SPLASH_CURRENT:
      return "Splash atual: ";
    case LOC_TEXT_SPLASH_UNKNOWN:
      return "modo de splash desconhecido";
    case LOC_TEXT_SPLASH_UPDATED:
      return "estado do splash atualizado";
    case LOC_TEXT_SPLASH_UPDATED_ON:
      return "splash habilitado para o proximo boot";
    case LOC_TEXT_SPLASH_UPDATED_OFF:
      return "splash desabilitado; logs serao exibidos no proximo boot";
    case LOC_TEXT_LANGUAGE_CURRENT:
      return "Idioma atual: ";
    case LOC_TEXT_LANGUAGE_LIST_HEADER:
      return "Idiomas disponiveis:\n";
    case LOC_TEXT_LANGUAGE_UNKNOWN:
      return "idioma desconhecido";
    case LOC_TEXT_LANGUAGE_UPDATED:
      return "idioma atualizado";
    case LOC_TEXT_LANGUAGE_UPDATED_SAVED:
      return "idioma atualizado e salvo";
    default:
      return "";
    }
  }
}
