#include "shell/commands.h"
#include "shell/core.h"

#include "core/user.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

static int role_is_admin(const struct user_record *user) {
  return user && shell_string_equal(user->role, "admin");
}

static int ensure_admin(struct shell_context *ctx) {
  const struct user_record *user = ctx && ctx->session ? session_user(ctx->session) : NULL;
  if (!role_is_admin(user)) {
    shell_print_error("Permissao negada: apenas admin pode executar este comando.");
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
    return ok ? 0 : -1;
  }
  return vfs_create(path, VFS_MODE_DIR, meta);
}

static int cmd_add_user(struct shell_context *ctx, int argc, char **argv) {
  if (shell_handle_help(argc, argv,
                        "add-user <usuario> <senha> [role]",
                        "Cria um novo usuario local. Roles aceitas: user, admin.")) {
    return 0;
  }
  if (argc < 3 || argc > 4) {
    shell_print_error("Uso: add-user <usuario> <senha> [role]");
    return -1;
  }
  if (ensure_admin(ctx) != 0) {
    return -1;
  }

  const char *username = argv[1];
  const char *password = argv[2];
  const char *role = (argc == 4) ? argv[3] : "user";

  if (validate_username(username) != 0) {
    shell_print_error("Nome de usuario invalido.");
    return -1;
  }
  if (!password || password[0] == '\0') {
    shell_print_error("Senha obrigatoria.");
    return -1;
  }
  if (validate_role(role) != 0) {
    shell_print_error("Role invalida. Use: user ou admin.");
    return -1;
  }
  if (userdb_find(username, NULL) == 0) {
    shell_print_error("Usuario ja existe.");
    return -1;
  }

  uint32_t uid = 1000;
  uint32_t gid = 1000;
  if (userdb_next_ids(&uid, &gid) != 0) {
    shell_print_error("Falha ao reservar UID/GID.");
    return -1;
  }

  struct vfs_metadata home_root_meta = {0, 0, 0755};
  if (ensure_directory("/home", &home_root_meta) != 0) {
    shell_print_error("Falha ao preparar diretorio /home.");
    return -1;
  }

  char home[USER_HOME_MAX];
  build_home_path(username, home, sizeof(home));
  if (!home[0]) {
    shell_print_error("Falha ao montar caminho de home.");
    return -1;
  }

  struct vfs_metadata home_meta = {uid, gid, 0700};
  if (ensure_directory(home, &home_meta) != 0) {
    shell_print_error("Falha ao criar diretorio home do usuario.");
    return -1;
  }

  struct user_record user;
  if (user_record_init(username, password, role, uid, gid, home, &user) != 0) {
    shell_print_error("Falha ao montar registro do usuario.");
    return -1;
  }
  if (userdb_add(&user) != 0) {
    shell_print_error("Falha ao gravar usuario em /etc/users.db.");
    return -1;
  }

  (void)vfs_set_metadata(home, &home_meta);

  shell_print_ok("usuario criado");
  shell_print("usuario=");
  shell_print(username);
  shell_print(" uid=");
  shell_print_number(uid);
  shell_print(" role=");
  shell_print(role);
  shell_newline();
  return 0;
}

static int cmd_set_pass(struct shell_context *ctx, int argc, char **argv) {
  if (shell_handle_help(argc, argv,
                        "set-pass <usuario> <nova_senha>",
                        "Altera a senha de um usuario. Admin altera qualquer conta; usuario comum apenas a propria.")) {
    return 0;
  }
  if (argc != 3) {
    shell_print_error("Uso: set-pass <usuario> <nova_senha>");
    return -1;
  }

  const struct user_record *current = ctx && ctx->session ? session_user(ctx->session) : NULL;
  if (!current || !current->username[0]) {
    shell_print_error("Sessao invalida.");
    return -1;
  }

  const char *target_user = argv[1];
  const char *new_password = argv[2];
  if (!new_password || new_password[0] == '\0') {
    shell_print_error("Nova senha obrigatoria.");
    return -1;
  }

  int is_self = shell_string_equal(current->username, target_user);
  if (!is_self && !role_is_admin(current)) {
    shell_print_error("Permissao negada: apenas admin altera senha de outros usuarios.");
    return -1;
  }

  if (userdb_set_password(target_user, new_password) != 0) {
    shell_print_error("Falha ao atualizar senha (usuario inexistente ou erro de escrita).");
    return -1;
  }

  shell_print_ok("senha atualizada");
  shell_print("usuario=");
  shell_print(target_user);
  shell_newline();
  return 0;
}

static int cmd_list_users(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  if (shell_handle_help(argc, argv,
                        "list-users",
                        "Lista os usuarios cadastrados em /etc/users.db.")) {
    return 0;
  }
  if (argc != 1) {
    shell_print_error("Uso: list-users");
    return -1;
  }

  char *db = NULL;
  size_t len = 0;
  if (shell_read_file(USER_DB_PATH, &db, &len) != 0 || !db) {
    shell_print_error("Nao foi possivel ler /etc/users.db.");
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
    shell_print("(sem usuarios cadastrados)");
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
