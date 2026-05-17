/*
 * src/apps/settings_actions.c
 *
 * Inline-prompt action callbacks for the Settings application:
 *
 *   - theme / keyboard / language apply (persist to /system/config.ini
 *     + update the in-memory g_shell_settings buffer so the desktop
 *     loop sees the change on the next frame).
 *   - user creation via chained inline_prompt (username -> password).
 *   - browser homepage edit.
 *
 * Carved out of the pre-split monolith `src/apps/settings.c` at the
 * 2026-05-15 refactor so each translation unit stays under the
 * 900-line layout limit.
 *
 * Static gates:
 *   - settings_validate_username
 *   - settings_require_admin
 * are exposed via `internal/settings_internal.h` so the click router
 * in settings.c can run them before invoking apply/start callbacks.
 */
#include "apps/settings.h"
#include "auth/privilege.h"
#include "auth/session.h"
#include "auth/user.h"
#include "auth/user_home.h"
#include "auth/user_prefs.h"
#include "core/system_init.h"
#include "drivers/input/keyboard.h"
#include "gui/compositor.h"
#include "gui/inline_prompt.h"
#include "lang/app_language.h"
#include "lang/localization.h"
#include "memory/kmem.h"
#include "util/kstring.h"

#include "apps/internal/settings_internal.h"

#include <stddef.h>
#include <stdint.h>

/* Etapa F4 settings-actions (2026-05-03): in-memory settings buffer
 * declared in arch/x86_64/kernel_io_helpers.c. Updates here are
 * detected by the desktop loop and re-applied on the next frame. */
extern struct system_settings g_shell_settings;

/* Buffer estatico usado para encadeamento username -> password no
 * fluxo de criacao de usuario via inline_prompt. inline_prompt
 * dispara on_submit() e fecha; o callback de username re-abre um
 * novo prompt para password lendo de g_pending_username. */
char g_pending_username[USER_NAME_MAX] = {0};

static int settings_valid_username_char(char c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  return c == '-' || c == '_';
}

int settings_validate_username(const char *name) {
  size_t len = 0;
  if (!name || !name[0]) return -1;
  while (name[len]) {
    if (!settings_valid_username_char(name[len])) return -1;
    len++;
  }
  return (len < USER_NAME_MAX) ? 0 : -1;
}

int settings_require_admin(struct settings_app *app) {
  struct session_context *sess = session_active();
  const struct user_record *user = sess ? session_user(sess) : NULL;
  if (privilege_user_is_admin(user)) return 0;
  privilege_log_denied("settings-add-user", user);
  settings_set_status(app,
                      APP_T("Permissao negada: admin necessario",
                            "Permission denied: admin required",
                            "Permiso denegado: se requiere admin"),
                      0x00F38BA8);
  return -1;
}

/* === Apply helpers =====================================================
 * Cada acao atualiza /system/config.ini (persistencia) E o buffer
 * em memoria g_shell_settings (visibilidade imediata) sem precisar
 * de acesso ao desktop_session. */

void apply_theme_choice(const char *theme) {
  if (!theme || !theme[0]) return;
  if (system_save_theme(theme) == 0) {
    kstrcpy(g_shell_settings.theme, sizeof(g_shell_settings.theme), theme);
  }
}

void apply_keyboard_choice(const char *layout) {
  if (!layout || !layout[0]) return;
  /* Aplica imediatamente em runtime + persiste. */
  (void)keyboard_set_layout_by_name(layout);
  if (system_save_keyboard_layout(layout) == 0) {
    kstrcpy(g_shell_settings.keyboard_layout,
            sizeof(g_shell_settings.keyboard_layout), layout);
  }
}

void apply_language_choice(const char *lang) {
  if (!lang || !lang[0]) return;
  const char *normalized = localization_normalize_language(lang);
  if (!normalized) normalized = lang;
  struct session_context *sess = session_active();
  if (sess) {
    kstrcpy(sess->prefs.language, sizeof(sess->prefs.language), normalized);
    const struct user_record *user = session_user(sess);
    if (user) (void)user_prefs_save_language(user, normalized);
  }
}

/* Callback do segundo prompt (password) na criacao de usuario. */
static void on_password_submit(const char *pwd, void *ctx) {
  struct settings_app *app = (struct settings_app *)ctx;
  struct session_context *previous_session = NULL;
  const char *lang = app_current_language();
  uint32_t uid = 0u, gid = 0u;
  char home[USER_HOME_MAX];
  struct user_record rec;
  if (!app) app = &g_settings;
  if (!g_pending_username[0]) return;
  if (settings_require_admin(app) != 0) {
    g_pending_username[0] = '\0';
    return;
  }
  if (!pwd || !pwd[0]) {
    settings_set_status(app,
                        APP_T("Senha obrigatoria", "Password required",
                              "Contrasena obligatoria"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (!lang || !lang[0]) lang = "pt-BR";
  user_record_clear(&rec);
  previous_session = session_active();
  session_set_active(NULL);
  if (userdb_ensure() != 0) {
    session_set_active(previous_session);
    settings_set_status(app,
                        APP_T("Falha ao preparar usuarios",
                              "User database unavailable",
                              "Base de usuarios no disponible"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (userdb_find(g_pending_username, NULL) == 0) {
    session_set_active(previous_session);
    settings_set_status(app,
                        APP_T("Usuario ja existe", "User already exists",
                              "Usuario ya existe"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (userdb_next_ids(&uid, &gid) != 0) {
    session_set_active(previous_session);
    settings_set_status(app,
                        APP_T("Falha ao reservar UID", "Failed to reserve UID",
                              "Fallo al reservar UID"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  kstrcpy(home, sizeof(home), "/home/");
  size_t hl = kstrlen(home);
  for (size_t i = 0; g_pending_username[i] && hl + 1 < sizeof(home); ++i) {
    home[hl++] = g_pending_username[i];
  }
  home[hl] = '\0';
  if (user_record_init(g_pending_username, pwd, "user", uid, gid,
                       home, &rec) != 0) {
    session_set_active(previous_session);
    user_record_clear(&rec);
    settings_set_status(app,
                        APP_T("Senha nao atende a politica",
                              "Password does not meet policy",
                              "La contrasena no cumple la politica"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (user_home_prepare(home, uid, gid) != 0) {
    session_set_active(previous_session);
    user_record_clear(&rec);
    settings_set_status(app,
                        APP_T("Falha ao criar home", "Failed to create home",
                              "Fallo al crear home"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (userdb_add(&rec) == 0) {
    (void)user_prefs_save_language(&rec, lang ? lang : "pt-BR");
    settings_set_status(app,
                        APP_T("Usuario criado", "User created",
                              "Usuario creado"),
                        0x00A6E3A1);
  } else {
    settings_set_status(app,
                        APP_T("Falha ao salvar usuario",
                              "Failed to save user",
                              "Fallo al guardar usuario"),
                        0x00F38BA8);
  }
  session_set_active(previous_session);
  user_record_clear(&rec);
  g_pending_username[0] = '\0';
}

/* Callback do primeiro prompt (username). Re-abre um segundo
 * prompt para password mantendo o nome digitado em
 * g_pending_username. */
static void on_username_submit(const char *name, void *ctx) {
  struct settings_app *app = (struct settings_app *)ctx;
  struct session_context *previous_session = NULL;
  if (!app) app = &g_settings;
  if (!name || !name[0]) {
    g_pending_username[0] = '\0';
    return;
  }
  if (settings_require_admin(app) != 0) {
    g_pending_username[0] = '\0';
    return;
  }
  if (settings_validate_username(name) != 0) {
    settings_set_status(app,
                        APP_T("Nome de usuario invalido",
                              "Invalid username",
                              "Nombre de usuario invalido"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  previous_session = session_active();
  session_set_active(NULL);
  if (userdb_ensure() != 0) {
    session_set_active(previous_session);
    settings_set_status(app,
                        APP_T("Falha ao preparar usuarios",
                              "User database unavailable",
                              "Base de usuarios no disponible"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  if (userdb_find(name, NULL) == 0) {
    session_set_active(previous_session);
    settings_set_status(app,
                        APP_T("Usuario ja existe", "User already exists",
                              "Usuario ya existe"),
                        0x00F38BA8);
    g_pending_username[0] = '\0';
    return;
  }
  session_set_active(previous_session);
  kstrcpy(g_pending_username, sizeof(g_pending_username), name);
  settings_set_status(app,
                      APP_T("Digite a senha do novo usuario",
                            "Type the new user's password",
                            "Escriba la contrasena del usuario"),
                      0x00A6E3A1);
  if (inline_prompt_show_secret("Password:", "",
                                app->window ? app->window->frame.x + 180 : 200,
                                app->window ? app->window->frame.y + 150 : 220,
                                on_password_submit, app) != 0) {
    g_pending_username[0] = '\0';
    settings_set_status(app,
                        APP_T("Falha ao abrir prompt de senha",
                              "Failed to open password prompt",
                              "Fallo al abrir el prompt de contrasena"),
                        0x00F38BA8);
  }
}

void start_user_creation(void) {
  g_pending_username[0] = '\0';
  if (settings_require_admin(&g_settings) != 0) return;
  settings_set_status(&g_settings,
                      APP_T("Digite o nome do novo usuario",
                            "Type the new username",
                            "Escriba el nuevo usuario"),
                      0x00A6E3A1);
  if (inline_prompt_show("New username:", "",
                         g_settings.window ? g_settings.window->frame.x + 180 : 200,
                         g_settings.window ? g_settings.window->frame.y + 128 : 200,
                         on_username_submit, &g_settings) != 0) {
    settings_set_status(&g_settings,
                        APP_T("Falha ao abrir prompt de usuario",
                              "Failed to open username prompt",
                              "Fallo al abrir el prompt de usuario"),
                        0x00F38BA8);
  }
}

/* Callback do prompt de homepage do browser. */
static void on_homepage_submit(const char *url, void *ctx) {
  (void)ctx;
  const char *value = (url && url[0]) ? url : "https://wikipedia.org";
  struct system_settings live;
  system_load_settings(&live);
  kstrcpy(live.browser_homepage, sizeof(live.browser_homepage), value);
  if (system_save_settings(&live) == 0) {
    kstrcpy(g_shell_settings.browser_homepage,
            sizeof(g_shell_settings.browser_homepage), value);
  }
}

void start_homepage_edit(void) {
  struct system_settings live;
  const char *cur = "https://wikipedia.org";
  if (system_load_settings(&live) == 0 && live.browser_homepage[0]) {
    cur = live.browser_homepage;
  }
  inline_prompt_show("Homepage:", cur,
                     200, 220, on_homepage_submit, NULL);
}
