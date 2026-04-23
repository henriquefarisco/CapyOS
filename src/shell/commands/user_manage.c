#include "shell/commands.h"
#include "shell/core.h"

#include "lang/localization.h"
#include "auth/user.h"
#include "auth/user_prefs.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

enum user_manage_text_id {
  USER_MANAGE_DENIED = 0,
  USER_MANAGE_HELP_ADD,
  USER_MANAGE_HELP_PASS,
  USER_MANAGE_HELP_LIST,
  USER_MANAGE_USAGE_ADD,
  USER_MANAGE_INVALID_USERNAME,
  USER_MANAGE_PASSWORD_REQUIRED,
  USER_MANAGE_INVALID_ROLE,
  USER_MANAGE_ALREADY_EXISTS,
  USER_MANAGE_UID_RESERVE_FAILED,
  USER_MANAGE_HOME_ROOT_FAILED,
  USER_MANAGE_HOME_PATH_FAILED,
  USER_MANAGE_HOME_CREATE_FAILED,
  USER_MANAGE_RECORD_BUILD_FAILED,
  USER_MANAGE_DB_WRITE_FAILED,
  USER_MANAGE_PREFS_WARNING,
  USER_MANAGE_USER_CREATED,
  USER_MANAGE_LABEL_USER,
  USER_MANAGE_LABEL_UID,
  USER_MANAGE_LABEL_ROLE,
  USER_MANAGE_USAGE_PASS,
  USER_MANAGE_INVALID_SESSION,
  USER_MANAGE_NEW_PASSWORD_REQUIRED,
  USER_MANAGE_PASS_DENIED,
  USER_MANAGE_PASS_UPDATE_FAILED,
  USER_MANAGE_PASS_UPDATED,
  USER_MANAGE_USAGE_LIST,
  USER_MANAGE_DB_READ_FAILED,
  USER_MANAGE_NO_USERS,
};

static const char *user_manage_text(const char *language,
                                    enum user_manage_text_id id) {
  switch (id) {
  case USER_MANAGE_DENIED:
    return localization_select(
        language,
        "Permissao negada: apenas admin pode executar este comando.",
        "Permission denied: only admin can run this command.",
        "Permiso denegado: solo admin puede ejecutar este comando.");
  case USER_MANAGE_HELP_ADD:
    return localization_select(
        language,
        "Cria um novo usuario local. Roles aceitas: user, admin.",
        "Creates a new local user. Accepted roles: user, admin.",
        "Crea un nuevo usuario local. Roles aceptados: user, admin.");
  case USER_MANAGE_HELP_PASS:
    return localization_select(
        language,
        "Altera a senha de um usuario. Admin altera qualquer conta; usuario comum apenas a propria.",
        "Changes a user's password. Admin may change any account; regular users can change only their own.",
        "Cambia la contrasena de un usuario. Admin puede cambiar cualquier cuenta; un usuario comun solo la suya.");
  case USER_MANAGE_HELP_LIST:
    return localization_select(
        language,
        "Lista os usuarios cadastrados em /etc/users.db.",
        "Lists users registered in /etc/users.db.",
        "Lista los usuarios registrados en /etc/users.db.");
  case USER_MANAGE_USAGE_ADD:
    return "add-user <username> <password> [role]";
  case USER_MANAGE_INVALID_USERNAME:
    return localization_select(language, "Nome de usuario invalido.",
                               "Invalid username.",
                               "Nombre de usuario invalido.");
  case USER_MANAGE_PASSWORD_REQUIRED:
    return localization_select(language, "Senha obrigatoria.",
                               "Password is required.",
                               "La contrasena es obligatoria.");
  case USER_MANAGE_INVALID_ROLE:
    return localization_select(language, "Role invalida. Use: user ou admin.",
                               "Invalid role. Use: user or admin.",
                               "Role invalido. Usa: user o admin.");
  case USER_MANAGE_ALREADY_EXISTS:
    return localization_select(language, "Usuario ja existe.",
                               "User already exists.",
                               "El usuario ya existe.");
  case USER_MANAGE_UID_RESERVE_FAILED:
    return localization_select(language, "Falha ao reservar UID/GID.",
                               "Failed to reserve UID/GID.",
                               "Fallo al reservar UID/GID.");
  case USER_MANAGE_HOME_ROOT_FAILED:
    return localization_select(language, "Falha ao preparar diretorio /home.",
                               "Failed to prepare the /home directory.",
                               "Fallo al preparar el directorio /home.");
  case USER_MANAGE_HOME_PATH_FAILED:
    return localization_select(language, "Falha ao montar caminho de home.",
                               "Failed to build the home path.",
                               "Fallo al montar la ruta home.");
  case USER_MANAGE_HOME_CREATE_FAILED:
    return localization_select(language,
                               "Falha ao criar diretorio home do usuario.",
                               "Failed to create the user's home directory.",
                               "Fallo al crear el directorio home del usuario.");
  case USER_MANAGE_RECORD_BUILD_FAILED:
    return localization_select(language, "Falha ao montar registro do usuario.",
                               "Failed to build the user record.",
                               "Fallo al montar el registro del usuario.");
  case USER_MANAGE_DB_WRITE_FAILED:
    return localization_select(language,
                               "Falha ao gravar usuario em /etc/users.db.",
                               "Failed to write user to /etc/users.db.",
                               "Fallo al guardar el usuario en /etc/users.db.");
  case USER_MANAGE_PREFS_WARNING:
    return localization_select(
        language,
        "Aviso: nao foi possivel inicializar preferencias do usuario; sera usado o idioma padrao do sistema.\n",
        "Warning: could not initialize user preferences; the system default language will be used.\n",
        "Aviso: no fue posible inicializar las preferencias del usuario; se usara el idioma predeterminado del sistema.\n");
  case USER_MANAGE_USER_CREATED:
    return localization_select(language, "usuario criado", "user created",
                               "usuario creado");
  case USER_MANAGE_LABEL_USER:
    return localization_select(language, "usuario=", "user=", "usuario=");
  case USER_MANAGE_LABEL_UID:
    return " uid=";
  case USER_MANAGE_LABEL_ROLE:
    return " role=";
  case USER_MANAGE_USAGE_PASS:
    return "set-pass <username> <new_password>";
  case USER_MANAGE_INVALID_SESSION:
    return localization_select(language, "Sessao invalida.",
                               "Invalid session.",
                               "Sesion invalida.");
  case USER_MANAGE_NEW_PASSWORD_REQUIRED:
    return localization_select(language, "Nova senha obrigatoria.",
                               "New password is required.",
                               "La nueva contrasena es obligatoria.");
  case USER_MANAGE_PASS_DENIED:
    return localization_select(
        language,
        "Permissao negada: apenas admin altera senha de outros usuarios.",
        "Permission denied: only admin can change other users' passwords.",
        "Permiso denegado: solo admin puede cambiar la contrasena de otros usuarios.");
  case USER_MANAGE_PASS_UPDATE_FAILED:
    return localization_select(
        language,
        "Falha ao atualizar senha (usuario inexistente ou erro de escrita).",
        "Failed to update password (missing user or write error).",
        "Fallo al actualizar la contrasena (usuario inexistente o error de escritura).");
  case USER_MANAGE_PASS_UPDATED:
    return localization_select(language, "senha atualizada",
                               "password updated",
                               "contrasena actualizada");
  case USER_MANAGE_USAGE_LIST:
    return "list-users";
  case USER_MANAGE_DB_READ_FAILED:
    return localization_select(language, "Nao foi possivel ler /etc/users.db.",
                               "Could not read /etc/users.db.",
                               "No fue posible leer /etc/users.db.");
  case USER_MANAGE_NO_USERS:
  default:
    return localization_select(language, "(sem usuarios cadastrados)",
                               "(no users registered)",
                               "(sin usuarios registrados)");
  }
}

static int role_is_admin(const struct user_record *user) {
  return user && shell_string_equal(user->role, "admin");
}

static int ensure_admin(struct shell_context *ctx) {
  const char *language = shell_current_language();
  const struct user_record *user =
      ctx && ctx->session ? session_user(ctx->session) : NULL;
  if (!role_is_admin(user)) {
    shell_print_error(user_manage_text(language, USER_MANAGE_DENIED));
    return -1;
  }
  return 0;
}

static int valid_username_char(char c) {
  if (c >= 'a' && c <= 'z') {
    return 1;
  }
  if (c >= 'A' && c <= 'Z') {
    return 1;
  }
  if (c >= '0' && c <= '9') {
    return 1;
  }
  return c == '-' || c == '_';
}

static int validate_username(const char *username) {
  size_t len = shell_cstring_length(username);
  if (!username || len == 0 || len >= USER_NAME_MAX) {
    return -1;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!valid_username_char(username[i])) {
      return -1;
    }
  }
  return 0;
}

static int validate_role(const char *role) {
  if (!role || !role[0]) {
    return -1;
  }
  if (shell_string_equal(role, "user") || shell_string_equal(role, "admin")) {
    return 0;
  }
  return -1;
}

static void build_home_path(const char *username, char *out, size_t out_len) {
  shell_copy(out, out_len, "/home/");
  size_t base = shell_cstring_length(out);
  size_t name_len = shell_cstring_length(username);
  if (!username || base + name_len >= out_len) {
    return;
  }
  for (size_t i = 0; i < name_len; ++i) {
    out[base + i] = username[i];
  }
  out[base + name_len] = '\0';
}

static int ensure_directory(const char *path, const struct vfs_metadata *meta) {
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) == 0 && d) {
    int ok = d->inode && (d->inode->mode & VFS_MODE_DIR);
    if (d->refcount) {
      d->refcount--;
    }
    if (ok && meta) {
      (void)vfs_set_metadata(path, meta);
    }
    return ok ? 0 : -1;
  }
  if (vfs_create(path, VFS_MODE_DIR, meta) != 0) {
    return -1;
  }
  if (meta) {
    (void)vfs_set_metadata(path, meta);
  }
  return 0;
}

static int cmd_add_user(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  if (shell_handle_help(argc, argv, user_manage_text(language, USER_MANAGE_USAGE_ADD),
                        user_manage_text(language, USER_MANAGE_HELP_ADD))) {
    return 0;
  }
  if (argc < 3 || argc > 4) {
    shell_print_error(
        localization_select(language, "Uso: ", "Usage: ", "Uso: "));
    shell_print(user_manage_text(language, USER_MANAGE_USAGE_ADD));
    shell_newline();
    return -1;
  }
  if (ensure_admin(ctx) != 0) {
    return -1;
  }

  const char *username = argv[1];
  const char *password = argv[2];
  const char *role = (argc == 4) ? argv[3] : "user";

  if (validate_username(username) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_INVALID_USERNAME));
    return -1;
  }
  if (!password || password[0] == '\0') {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_PASSWORD_REQUIRED));
    return -1;
  }
  if (validate_role(role) != 0) {
    shell_print_error(user_manage_text(language, USER_MANAGE_INVALID_ROLE));
    return -1;
  }
  if (userdb_find(username, NULL) == 0) {
    shell_print_error(user_manage_text(language, USER_MANAGE_ALREADY_EXISTS));
    return -1;
  }

  uint32_t uid = 1000;
  uint32_t gid = 1000;
  struct session_context *previous_session = session_active();
  const char *default_language =
      (ctx && ctx->settings && ctx->settings->language[0])
          ? ctx->settings->language
          : "en";
  int rc = -1;
  if (userdb_next_ids(&uid, &gid) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_UID_RESERVE_FAILED));
    return -1;
  }

  struct vfs_metadata home_root_meta = {0, 0, 0755};
  session_set_active(NULL);
  if (ensure_directory("/home", &home_root_meta) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_HOME_ROOT_FAILED));
    goto done;
  }

  char home[USER_HOME_MAX];
  build_home_path(username, home, sizeof(home));
  if (!home[0]) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_HOME_PATH_FAILED));
    goto done;
  }

  struct vfs_metadata home_meta = {uid, gid, 0700};
  if (ensure_directory(home, &home_meta) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_HOME_CREATE_FAILED));
    goto done;
  }

  struct user_record user;
  if (user_record_init(username, password, role, uid, gid, home, &user) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_RECORD_BUILD_FAILED));
    goto done;
  }
  if (userdb_add(&user) != 0) {
    shell_print_error(user_manage_text(language, USER_MANAGE_DB_WRITE_FAILED));
    goto done;
  }

  (void)vfs_set_metadata(home, &home_meta);
  if (user_prefs_save_language(&user, default_language) != 0) {
    shell_print(user_manage_text(language, USER_MANAGE_PREFS_WARNING));
  }

  shell_print_ok(user_manage_text(language, USER_MANAGE_USER_CREATED));
  shell_print(user_manage_text(language, USER_MANAGE_LABEL_USER));
  shell_print(username);
  shell_print(user_manage_text(language, USER_MANAGE_LABEL_UID));
  shell_print_number(uid);
  shell_print(user_manage_text(language, USER_MANAGE_LABEL_ROLE));
  shell_print(role);
  shell_newline();
  rc = 0;

done:
  session_set_active(previous_session);
  return rc;
}

static int cmd_set_pass(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  if (shell_handle_help(argc, argv,
                        user_manage_text(language, USER_MANAGE_USAGE_PASS),
                        user_manage_text(language, USER_MANAGE_HELP_PASS))) {
    return 0;
  }
  if (argc != 3) {
    shell_print_error(localization_select(language, "Uso: ", "Usage: ", "Uso: "));
    shell_print(user_manage_text(language, USER_MANAGE_USAGE_PASS));
    shell_newline();
    return -1;
  }

  const struct user_record *current =
      ctx && ctx->session ? session_user(ctx->session) : NULL;
  if (!current || !current->username[0]) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_INVALID_SESSION));
    return -1;
  }

  const char *target_user = argv[1];
  const char *new_password = argv[2];
  if (!new_password || new_password[0] == '\0') {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_NEW_PASSWORD_REQUIRED));
    return -1;
  }

  int is_self = shell_string_equal(current->username, target_user);
  if (!is_self && !role_is_admin(current)) {
    shell_print_error(user_manage_text(language, USER_MANAGE_PASS_DENIED));
    return -1;
  }

  if (userdb_set_password(target_user, new_password) != 0) {
    shell_print_error(
        user_manage_text(language, USER_MANAGE_PASS_UPDATE_FAILED));
    return -1;
  }

  shell_print_ok(user_manage_text(language, USER_MANAGE_PASS_UPDATED));
  shell_print(user_manage_text(language, USER_MANAGE_LABEL_USER));
  shell_print(target_user);
  shell_newline();
  return 0;
}

static int cmd_list_users(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv,
                        user_manage_text(language, USER_MANAGE_USAGE_LIST),
                        user_manage_text(language, USER_MANAGE_HELP_LIST))) {
    return 0;
  }
  if (argc != 1) {
    shell_print_error(localization_select(language, "Uso: ", "Usage: ", "Uso: "));
    shell_print(user_manage_text(language, USER_MANAGE_USAGE_LIST));
    shell_newline();
    return -1;
  }

  char *db = NULL;
  size_t len = 0;
  if (shell_read_file(USER_DB_PATH, &db, &len) != 0 || !db) {
    shell_print_error(user_manage_text(language, USER_MANAGE_DB_READ_FAILED));
    return -1;
  }

  size_t line_start = 0;
  uint32_t count = 0;
  for (size_t i = 0; i <= len; ++i) {
    if (i == len || db[i] == '\n') {
      if (i > line_start) {
        size_t first_sep = line_start;
        while (first_sep < i && db[first_sep] != ':') {
          ++first_sep;
        }
        if (first_sep > line_start) {
          char username[USER_NAME_MAX];
          size_t ulen = first_sep - line_start;
          if (ulen >= sizeof(username)) {
            ulen = sizeof(username) - 1;
          }
          for (size_t k = 0; k < ulen; ++k) {
            username[k] = db[line_start + k];
          }
          username[ulen] = '\0';
          shell_print(username);
          shell_newline();
          ++count;
        }
      }
      line_start = i + 1;
    }
  }
  kfree(db);

  if (count == 0) {
    shell_print(user_manage_text(language, USER_MANAGE_NO_USERS));
    shell_newline();
  }
  return 0;
}

static struct shell_command g_user_manage_commands[3];
static int g_user_manage_commands_initialized = 0;

static void init_user_manage_commands(void) {
  if (g_user_manage_commands_initialized) {
    return;
  }
  g_user_manage_commands[0].name = "add-user";
  g_user_manage_commands[0].handler = cmd_add_user;
  g_user_manage_commands[1].name = "set-pass";
  g_user_manage_commands[1].handler = cmd_set_pass;
  g_user_manage_commands[2].name = "list-users";
  g_user_manage_commands[2].handler = cmd_list_users;
  g_user_manage_commands_initialized = 1;
}

const struct shell_command *shell_commands_user_manage(size_t *count) {
  init_user_manage_commands();
  if (count) {
    *count = 3;
  }
  return g_user_manage_commands;
}
