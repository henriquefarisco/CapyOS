/* system_init.c: system setup helpers (config, first-run, users, theme). */
#include "core/system_init.h"

#include "core/session.h"
#include "core/user.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

#include <stdint.h>

#define SYS_PATH_MAX 128
#define SETUP_LOG_CAPACITY 8192

static int ensure_directory(const char *path);
static int write_text_file(const char *path, const char *text);
static int g_setup_debug =
    0; /* pode ser ativado em tempo de execucao se precisar */

static char g_setup_log[SETUP_LOG_CAPACITY];
static size_t g_setup_log_len = 0;
static size_t g_setup_log_flushed = 0;
static int g_setup_log_ready = 0;

static size_t cstring_length(const char *s) {
  size_t len = 0;
  if (!s) {
    return 0;
  }
  while (s[len]) {
    ++len;
  }
  return len;
}

static void memory_zero(void *dst, size_t len) {
  uint8_t *p = (uint8_t *)dst;
  while (len--) {
    *p++ = 0;
  }
}

static void cstring_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) {
    return;
  }
  size_t i = 0;
  if (src) {
    while (src[i] && i < dst_size - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int strings_equal(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  size_t i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

static void system_settings_set_defaults(struct system_settings *settings) {
  if (!settings) {
    return;
  }
  cstring_copy(settings->hostname, sizeof(settings->hostname), "capyos-node");
  cstring_copy(settings->theme, sizeof(settings->theme), "noir");
  cstring_copy(settings->keyboard_layout, sizeof(settings->keyboard_layout),
               "us");
  settings->splash_enabled = 1;
  settings->diagnostics_enabled = 0;
}

static int config_line_equals(const char *line, size_t len, const char *key,
                              const char *value) {
  if (!line || !key || !value) {
    return 0;
  }
  size_t key_len = cstring_length(key);
  size_t value_len = cstring_length(value);
  if (len != key_len + 1 + value_len) {
    return 0;
  }
  for (size_t i = 0; i < key_len; ++i) {
    if (line[i] != key[i]) {
      return 0;
    }
  }
  if (line[key_len] != '=') {
    return 0;
  }
  for (size_t i = 0; i < value_len; ++i) {
    if (line[key_len + 1 + i] != value[i]) {
      return 0;
    }
  }
  return 1;
}

static int verify_directory_exists(const char *path) {
  if (!path) {
    return -1;
  }
  struct vfs_stat st;
  if (vfs_stat_path(path, &st) != 0) {
    vga_write("Falha ao verificar diretorio: ");
    vga_write(path);
    vga_newline();
    return -1;
  }
  if ((st.mode & VFS_MODE_DIR) == 0) {
    vga_write("Item nao eh diretorio: ");
    vga_write(path);
    vga_newline();
    return -1;
  }
  return 0;
}

static void sync_root_device(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    buffer_cache_sync(root->bdev);
  }
}

static void buffer_append(char *dst, size_t dst_size, const char *src) {
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

static void bytes_to_hex_str(const uint8_t *src, size_t len, char *dst,
                             size_t dst_size) {
  static const char hex_digits[] = "0123456789abcdef";
  if (!dst || dst_size == 0) {
    return;
  }
  size_t needed = len * 2;
  size_t limit = (needed < dst_size - 1) ? needed : (dst_size - 1);
  size_t di = 0;
  for (size_t i = 0; i < len && (di + 1) < dst_size; ++i) {
    uint8_t v = src[i];
    if (di < limit) {
      dst[di++] = hex_digits[(v >> 4) & 0x0F];
    }
    if (di < limit) {
      dst[di++] = hex_digits[v & 0x0F];
    }
  }
  dst[di] = '\0';
}

static void log_buffer_append(const char *msg) {
  if (!msg) {
    return;
  }
  size_t len = cstring_length(msg);
  if (len == 0) {
    return;
  }
  if (g_setup_log_len >= SETUP_LOG_CAPACITY - 2) {
    return;
  }
  size_t space = SETUP_LOG_CAPACITY - 1 - g_setup_log_len;
  if (len > space) {
    len = space;
  }
  for (size_t i = 0; i < len; ++i) {
    g_setup_log[g_setup_log_len++] = msg[i];
  }
  if (g_setup_log_len < SETUP_LOG_CAPACITY - 1) {
    g_setup_log[g_setup_log_len++] = '\n';
  }
  g_setup_log[g_setup_log_len] = '\0';
}

static void log_flush_pending(void) {
  if (!g_setup_log_ready || g_setup_log_flushed >= g_setup_log_len) {
    return;
  }
  if (ensure_directory("/var") != 0) {
    return;
  }
  if (ensure_directory("/var/log") != 0) {
    return;
  }

  struct dentry *d = NULL;
  if (vfs_lookup("/var/log/setup.log", &d) != 0) {
    if (vfs_create("/var/log/setup.log", VFS_MODE_FILE, NULL) != 0) {
      return;
    }
  } else if (d && d->refcount) {
    d->refcount--;
  }

  struct file *f = vfs_open("/var/log/setup.log", VFS_OPEN_WRITE);
  if (!f) {
    return;
  }
  if (f->dentry && f->dentry->inode) {
    f->position = f->dentry->inode->size;
  }
  size_t pending = g_setup_log_len - g_setup_log_flushed;
  long written = vfs_write(f, &g_setup_log[g_setup_log_flushed], pending);
  if (written > 0) {
    g_setup_log_flushed += (size_t)written;
    if (g_setup_log_flushed > g_setup_log_len) {
      g_setup_log_flushed = g_setup_log_len;
    }
  }
  vfs_close(f);
}

static void log_event(const char *msg) {
  if (!msg) {
    return;
  }
  log_buffer_append(msg);
  log_flush_pending();
}

static void print_line(const char *msg) {
  if (!msg) {
    return;
  }
  vga_write(msg);
  vga_newline();
  log_buffer_append(msg);
  log_flush_pending();
}

static void log_emit_segments(const char *a, const char *b, const char *c,
                              const char *d, const char *e) {
  const char *parts[5] = {a, b, c, d, e};
  for (size_t i = 0; i < 5; ++i) {
    const char *seg = parts[i];
    if (!seg) {
      continue;
    }
    vga_write(seg);
    log_buffer_append(seg);
  }
  vga_newline();
  log_buffer_append("\n");
  log_flush_pending();
}

static void log_process_begin(const char *name) {
  log_emit_segments("Iniciando processo de ", name, "...", NULL, NULL);
}

static void log_process_begin_success(const char *name) {
  log_emit_segments("Processo ", name, " iniciado com sucesso...", NULL, NULL);
}

static void log_process_progress(const char *name) {
  log_emit_segments("Processo ", name, " em andamento.", NULL, NULL);
}

static void log_process_conclude(const char *name) {
  log_emit_segments("Processo ", name, " concluido.", NULL, NULL);
}

static void log_process_finalize(const char *name) {
  log_emit_segments("Finalizando processo ", name, "...", NULL, NULL);
}

static void log_process_finalize_success(const char *name) {
  log_emit_segments("Processo ", name, " finalizado com sucesso.", NULL, NULL);
}

static void log_dependency_wait(const char *dependency, const char *target) {
  log_emit_segments("Aguardando processo ", dependency,
                    " para iniciar o processo ", target, "...");
}

static int verify_config_file(const char *hostname, const char *theme,
                              const char *keyboard, int splash_enabled) {
  const char *splash_value = splash_enabled ? "enabled" : "disabled";
  const char *keyboard_value = keyboard ? keyboard : "us";
  struct file *f = vfs_open("/system/config.ini", VFS_OPEN_READ);
  if (!f) {
    vga_write("Falha ao reabrir configuracao em /system/config.ini.\n");
    return -1;
  }
  char buffer[256];
  long read = vfs_read(f, buffer, sizeof(buffer) - 1);
  vfs_close(f);
  if (read <= 0) {
    vga_write("Arquivo de configuracao vazio ou inacessivel.\n");
    return -1;
  }
  buffer[read] = '\0';

  int hostname_ok = 0;
  int theme_ok = 0;
  int keyboard_ok = 0;
  int splash_ok = 0;

  size_t start = 0;
  for (size_t i = 0; i <= (size_t)read; ++i) {
    if (i == (size_t)read || buffer[i] == '\n') {
      size_t len = i - start;
      if (len > 0) {
        if (!hostname_ok &&
            config_line_equals(&buffer[start], len, "hostname", hostname)) {
          hostname_ok = 1;
        } else if (!theme_ok &&
                   config_line_equals(&buffer[start], len, "theme", theme)) {
          theme_ok = 1;
        } else if (!keyboard_ok &&
                   config_line_equals(&buffer[start], len, "keyboard",
                                      keyboard_value)) {
          keyboard_ok = 1;
        } else if (!splash_ok && config_line_equals(&buffer[start], len,
                                                    "splash", splash_value)) {
          splash_ok = 1;
        }
      }
      start = i + 1;
    }
  }

  if (!hostname_ok || !theme_ok || !keyboard_ok || !splash_ok) {
    vga_write("Falha ao validar conteudo de /system/config.ini.\n");
    return -1;
  }
  return 0;
}

static int write_settings_file(const struct system_settings *settings) {
  if (!settings) {
    return -1;
  }
  if (ensure_directory("/system") != 0) {
    return -1;
  }

  const char *splash_value = settings->splash_enabled ? "enabled" : "disabled";
  char config_buffer[256];
  size_t idx = 0;
  const char *entries[] = {"hostname=",
                           settings->hostname,
                           "\n",
                           "theme=",
                           settings->theme,
                           "\n",
                           "keyboard=",
                           settings->keyboard_layout,
                           "\n",
                           "splash=",
                           splash_value,
                           "\n"};
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
    const char *p = entries[i];
    size_t plen = cstring_length(p);
    for (size_t j = 0; j < plen && idx < sizeof(config_buffer) - 1; ++j) {
      config_buffer[idx++] = p[j];
    }
  }
  config_buffer[idx] = '\0';

  return write_text_file("/system/config.ini", config_buffer);
}

static void u32_to_string(uint32_t value, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) {
    return;
  }
  if (value == 0) {
    if (buf_len >= 2) {
      buf[0] = '0';
      buf[1] = '\0';
    } else {
      buf[0] = '\0';
    }
    return;
  }
  char tmp[10];
  size_t idx = 0;
  while (value && idx < sizeof(tmp)) {
    tmp[idx++] = (char)('0' + (value % 10));
    value /= 10;
  }
  size_t pos = 0;
  while (idx && pos + 1 < buf_len) {
    buf[pos++] = tmp[--idx];
  }
  buf[pos] = '\0';
}

static void log_user_record_state(const struct user_record *rec) {
  if (!rec) {
    return;
  }
  char uid_buf[12];
  char gid_buf[12];
  u32_to_string(rec->uid, uid_buf, sizeof(uid_buf));
  u32_to_string(rec->gid, gid_buf, sizeof(gid_buf));

  char salt_hex[USER_SALT_SIZE * 2 + 1];
  char hash_hex[USER_HASH_SIZE * 2 + 1];
  bytes_to_hex_str(rec->salt, USER_SALT_SIZE, salt_hex, sizeof(salt_hex));
  bytes_to_hex_str(rec->hash, USER_HASH_SIZE, hash_hex, sizeof(hash_hex));

  log_buffer_append("   userdb: usuario=");
  log_buffer_append(rec->username);
  log_buffer_append(" uid=");
  log_buffer_append(uid_buf);
  log_buffer_append(" gid=");
  log_buffer_append(gid_buf);
  log_buffer_append(" home=");
  log_buffer_append(rec->home);
  log_buffer_append(" salt=");
  log_buffer_append(salt_hex);
  log_buffer_append(" hash=");
  log_buffer_append(hash_hex);
  log_buffer_append("\n");
  log_flush_pending();
}

static size_t wizard_prompt(const char *prompt, char *buffer, size_t buffer_len,
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
  tty_set_prompt(prompt ? prompt : "");
  tty_show_prompt();
  size_t len = tty_readline(buffer, buffer_len);
  tty_set_echo(1);
  tty_set_echo_mask('\0');
  return len;
}

static int prompt_password_pair(const char *label, char *password,
                                size_t password_len) {
  for (int tries = 0; tries < 3; ++tries) {
    char confirm[TTY_BUFFER_MAX];
    memory_zero(password, password_len);
    memory_zero(confirm, sizeof(confirm));
    size_t plen = wizard_prompt(label, password, password_len, 1);
    size_t clen =
        wizard_prompt("Confirme a senha: ", confirm, sizeof(confirm), 1);
    if (plen == 0 || clen == 0) {
      print_line("Senha nao pode ser vazia.");
      continue;
    }
    if (plen != clen) {
      print_line("As senhas nao coincidem.");
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
      print_line("As senhas nao coincidem.");
      continue;
    }
    return 0;
  }
  return -1;
}

static const char *g_cli_reference_text =
    "CapyCLI - Referencia Rapida\n"
    "============================\n"
    "\n"
    "Navegacao\n"
    "  list [caminho]       - lista itens\n"
    "  go <caminho>         - altera diretorio atual\n"
    "  mypath               - mostra caminho corrente\n"
    "\n"
    "Arquivo\n"
    "  print-file <arq>     - mostra conteudo\n"
    "  page <arq>           - paginacao\n"
    "  mk-file <arq>        - cria arquivo vazio\n"
    "  mk-dir <dir>         - cria diretorio\n"
    "  kill-file <arq>      - remove arquivo\n"
    "  kill-dir <dir>       - remove diretorio vazio\n"
    "  clone <src> <dst>    - copia arquivo\n"
    "  move <src> <dst>     - move/renomeia\n"
    "  stats-file <alvo>    - exibe metadados\n"
    "  type <alvo>          - identifica tipo\n"
    "\n"
    "Busca\n"
    "  hunt-file <padrao> [onde]\n"
    "  hunt-dir  <padrao> [onde]\n"
    "  hunt-any  <padrao> [onde]\n"
    "  find "
    "texto"
    " [onde]    - procura conteudo\n"
    "\n"
    "Processos & Sistema\n"
    "  print-me             - usuario atual\n"
    "  print-id             - uid/gid\n"
    "  print-host           - hostname\n"
    "  print-time           - uptime\n"
    "  do-sync              - sincroniza discos\n"
    "  mess                 - limpa tela\n"
    "  bye                  - encerra sessao\n"
    "\n"
    "Ajuda\n"
    "  help-any             - lista comandos\n"
    "  help-docs            - exibe este resumo\n"
    "\n"
    "Observacao: caminhos relativos respeitam o diretorio ativo.\n";

static void dbg_print(const char *a, const char *b) {
  if (!g_setup_debug)
    return;
  if (a)
    vga_write(a);
  if (b)
    vga_write(b);
  vga_newline();
}

static void dbg_print_heap(const char *prefix, const char *path) {
  if (!g_setup_debug) {
    return;
  }
  char used[16];
  char total[16];
  u32_to_string((uint32_t)kheap_used(), used, sizeof(used));
  u32_to_string((uint32_t)kheap_size(), total, sizeof(total));
  if (prefix) {
    vga_write(prefix);
  }
  if (path) {
    vga_write(path);
  }
  vga_write(" [heap ");
  vga_write(used);
  vga_write("/");
  vga_write(total);
  vga_write("]");
  vga_newline();
}

static int ensure_directory(const char *path) {
  if (!path || path[0] != '/') {
    return -1;
  }
  char build[SYS_PATH_MAX];
  size_t build_len = 1;
  build[0] = '/';
  build[1] = '\0';

  const char *p = path;
  while (*p == '/') {
    ++p;
  }

  int guard = 0;
  while (*p) {
    const char *start = p;
    size_t len = 0;
    while (p[len] && p[len] != '/') {
      ++len;
    }
    if (len > 0) {
      if (build_len > 1) {
        if (build_len + 1 >= sizeof(build)) {
          dbg_print("[setup] ensure: path too long (add /) ", build);
          return -1;
        }
        build[build_len++] = '/';
      }
      if (build_len + len >= sizeof(build)) {
        dbg_print("[setup] ensure: path too long (segment) ", NULL);
        return -1;
      }
      for (size_t i = 0; i < len; ++i) {
        build[build_len++] = start[i];
      }
      build[build_len] = '\0';

      struct dentry *d = NULL;
      int lrc = vfs_lookup(build, &d);
      if (lrc != 0) {
        dbg_print_heap("[setup] mkdir: ", build);
        int crc = vfs_create(build, VFS_MODE_DIR, NULL);
        if (crc != 0) {
          dbg_print("[setup] mkdir failed: ", build);
          return -1;
        }
      } else {
        if (d && d->refcount) {
          d->refcount--;
        }
        dbg_print_heap("[setup] dir exists: ", build);
      }
    }
    p += len;
    while (*p == '/') {
      ++p;
    }
    if (++guard > 128) {
      dbg_print("[setup] ensure guard tripped for: ", path);
      return -1;
    }
  }
  return 0;
}

static int write_text_file(const char *path, const char *text) {
  if (!path || !text) {
    return -1;
  }
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
      return -1;
    }
  } else {
    if (d && d->refcount) {
      d->refcount--;
    }
  }
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) {
    return -1;
  }
  if (f->dentry && f->dentry->inode) {
    f->position = 0;
  }
  size_t len = cstring_length(text);
  long written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

int system_detect_first_boot(void) {
  struct dentry *marker = NULL;
  if (vfs_lookup("/system/first-run.done", &marker) == 0 && marker) {
    if (marker->refcount) {
      marker->refcount--;
    }
    return 0;
  }
  return 1;
}

static void build_home_path(const char *username, char *out, size_t out_len) {
  cstring_copy(out, out_len, "/home/");
  size_t base_len = cstring_length(out);
  size_t uname_len = cstring_length(username);
  if (base_len + uname_len >= out_len) {
    return;
  }
  for (size_t i = 0; i < uname_len; ++i) {
    out[base_len + i] = username[i];
  }
  out[base_len + uname_len] = '\0';
}

static const char *validate_theme(const char *input) {
  if (!input || cstring_length(input) == 0) {
    return "noir";
  }
  if (strings_equal(input, "noir") || strings_equal(input, "ocean") ||
      strings_equal(input, "forest")) {
    return input;
  }
  return "noir";
}

int system_mark_first_boot_complete(void) {
  if (ensure_directory("/system") != 0) {
    return -1;
  }
  return write_text_file("/system/first-run.done", "completed\n");
}

/* Implementacao da configuracao inicial. */
static int first_boot_setup_impl(void) {
  print_line("=== Assistente CapyOS - Configuracao Inicial ===");
  print_line(
      "Este assistente prepara usuarios, configuracao e estrutura basica.");
  vga_newline();

  g_setup_debug = 1; // habilita logs detalhados durante a preparacao inicial

  const char *directories[] = {"/bin", "/etc",     "/home",   "/tmp",
                               "/var", "/var/log", "/system", "/docs"};

  const char *proc_dirs = "preparacao de diretorios padrao";
  log_process_begin(proc_dirs);
  log_process_begin_success(proc_dirs);
  for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
    dbg_print_heap("[setup] ensure path: ", directories[i]);
    if (ensure_directory(directories[i]) != 0) {
      print_line("Falha ao preparar estrutura de diretorios.");
      return -1;
    }
  }
  log_process_progress(proc_dirs);
  for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
    if (verify_directory_exists(directories[i]) != 0) {
      print_line("Verificacao de diretorios falhou.");
      return -1;
    }
  }
  print_line("   Estrutura de diretorios pronta.");
  if (!g_setup_log_ready) {
    g_setup_log_ready = 1;
  }
  log_flush_pending();
  log_process_conclude(proc_dirs);
  log_process_finalize(proc_dirs);
  log_process_finalize_success(proc_dirs);

  struct vfs_metadata meta_tmp;
  meta_tmp.uid = 0;
  meta_tmp.gid = 0;
  meta_tmp.perm = 0777;
  vfs_set_metadata("/tmp", &meta_tmp);

  struct vfs_metadata meta_var = {0, 0, 0755};
  vfs_set_metadata("/var", &meta_var);

  struct vfs_metadata meta_var_log = {0, 0, 0777};
  vfs_set_metadata("/var/log", &meta_var_log);

  struct vfs_metadata meta_log_file = {0, 0, 0666};
  if (write_text_file("/var/log/cli-selftest.log", "") == 0) {
    vfs_set_metadata("/var/log/cli-selftest.log", &meta_log_file);
  }
  if (vfs_set_metadata("/var/log/setup.log", &meta_log_file) != 0) {
    write_text_file("/var/log/setup.log", "");
    vfs_set_metadata("/var/log/setup.log", &meta_log_file);
  }

  const char *proc_userdb = "preparacao da base de usuarios";
  log_dependency_wait(proc_dirs, proc_userdb);
  log_process_begin(proc_userdb);
  log_process_begin_success(proc_userdb);
  if (userdb_ensure() != 0) {
    print_line("Nao foi possivel criar /etc/users.db.");
    return -1;
  }
  if (verify_directory_exists("/etc") != 0) {
    print_line("Diretorio /etc inacessivel.");
    return -1;
  }
  log_process_progress(proc_userdb);
  struct vfs_stat userdb_stat;
  if (vfs_stat_path(USER_DB_PATH, &userdb_stat) != 0) {
    print_line("Nao foi possivel validar /etc/users.db.");
    return -1;
  }
  char userdb_size[12];
  u32_to_string(userdb_stat.size, userdb_size, sizeof(userdb_size));
  char size_msg[128];
  size_msg[0] = '\0';
  buffer_append(size_msg, sizeof(size_msg), "   /etc/users.db disponivel (");
  buffer_append(size_msg, sizeof(size_msg), userdb_size);
  buffer_append(size_msg, sizeof(size_msg), " bytes).");
  print_line(size_msg);
  sync_root_device();
  log_process_conclude(proc_userdb);
  log_process_finalize(proc_userdb);
  log_process_finalize_success(proc_userdb);

  const char *proc_docs = "instalacao da referencia CapyCLI";
  log_dependency_wait(proc_userdb, proc_docs);
  log_process_begin(proc_docs);
  log_process_begin_success(proc_docs);
  if (write_text_file("/docs/noiros-cli-reference.txt", g_cli_reference_text) !=
      0) {
    print_line("   Aviso: nao foi possivel gravar referencia do CapyCLI.");
  } else {
    print_line(
        "   Referencia CapyCLI pronta em /docs/noiros-cli-reference.txt.");
  }
  sync_root_device();
  log_process_conclude(proc_docs);
  log_process_finalize(proc_docs);
  log_process_finalize_success(proc_docs);

  char hostname[TTY_BUFFER_MAX];
  char theme_input[TTY_BUFFER_MAX];
  char splash_answer[TTY_BUFFER_MAX];
  memory_zero(hostname, sizeof(hostname));
  memory_zero(theme_input, sizeof(theme_input));
  memory_zero(splash_answer, sizeof(splash_answer));

  const char *proc_settings = "coleta de configuracoes basicas";
  log_dependency_wait(proc_docs, proc_settings);
  log_process_begin(proc_settings);
  log_process_begin_success(proc_settings);
  size_t hlen =
      wizard_prompt("Hostname [capyos-node]: ", hostname, sizeof(hostname), 0);
  if (hlen == 0) {
    cstring_copy(hostname, sizeof(hostname), "capyos-node");
  }
  char host_msg[128];
  host_msg[0] = '\0';
  buffer_append(host_msg, sizeof(host_msg), "   Hostname definido: ");
  buffer_append(host_msg, sizeof(host_msg), hostname);
  print_line(host_msg);

  print_line("Temas disponiveis: noir, ocean, forest.");
  size_t tlen =
      wizard_prompt("Tema [noir]: ", theme_input, sizeof(theme_input), 0);
  if (tlen == 0) {
    cstring_copy(theme_input, sizeof(theme_input), "noir");
  }
  const char *theme = validate_theme(theme_input);
  char theme_msg[128];
  theme_msg[0] = '\0';
  buffer_append(theme_msg, sizeof(theme_msg), "   Tema selecionado: ");
  buffer_append(theme_msg, sizeof(theme_msg), theme);
  print_line(theme_msg);

  size_t slen = wizard_prompt("Ativar splash animado? [S/n]: ", splash_answer,
                              sizeof(splash_answer), 0);
  int splash_enabled = 1;
  if (slen > 0 && (splash_answer[0] == 'n' || splash_answer[0] == 'N')) {
    splash_enabled = 0;
  }
  print_line(splash_enabled ? "   Splash animado: habilitado"
                            : "   Splash animado: desabilitado");

  const char *admin_username = "admin";
  uint32_t admin_uid = 1000;
  uint32_t admin_gid = 1000;
  char admin_password[TTY_BUFFER_MAX];
  memory_zero(admin_password, sizeof(admin_password));
  log_process_progress(proc_settings);
  const char *proc_admin = "provisionamento do usuario administrador";
  log_dependency_wait(proc_settings, proc_admin);
  log_process_begin(proc_admin);
  log_process_begin_success(proc_admin);
  char admin_home[USER_HOME_MAX];
  memory_zero(admin_home, sizeof(admin_home));
  build_home_path(admin_username, admin_home, sizeof(admin_home));
  if (ensure_directory(admin_home) != 0) {
    print_line("Falha ao criar diretorio pessoal do administrador.");
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  if (verify_directory_exists(admin_home) != 0) {
    print_line("Diretorio pessoal do administrador indisponivel.");
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  struct vfs_metadata home_meta = {admin_uid, admin_gid, 0700};
  if (vfs_set_metadata(admin_home, &home_meta) != 0) {
    print_line(
        "Aviso: nao foi possivel ajustar permissoes do diretorio pessoal.");
  }
  sync_root_device();

  int admin_ready = 0;
  struct user_record existing;
  if (userdb_find(admin_username, &existing) == 0) {
    print_line("   Usuario admin ja existente. Validando registro atual.");
    if (verify_directory_exists(existing.home) != 0) {
      char rebuild_msg[128];
      rebuild_msg[0] = '\0';
      buffer_append(rebuild_msg, sizeof(rebuild_msg),
                    "   Diretorio pessoal ausente. Recriando em ");
      buffer_append(rebuild_msg, sizeof(rebuild_msg), existing.home);
      buffer_append(rebuild_msg, sizeof(rebuild_msg), ".");
      print_line(rebuild_msg);
      if (ensure_directory(existing.home) != 0 ||
          verify_directory_exists(existing.home) != 0) {
        print_line("   Falha ao reconstruir diretorio pessoal do admin.");
        return -1;
      }
    }
    admin_uid = existing.uid;
    admin_gid = existing.gid;
    admin_ready = 1;
  }

  while (!admin_ready) {
    if (prompt_password_pair("Defina a senha para o usuario admin: ",
                             admin_password, sizeof(admin_password)) != 0) {
      print_line("Nao foi possivel registrar o usuario administrador.");
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    struct user_record admin;
    if (user_record_init(admin_username, admin_password, "admin", admin_uid,
                         admin_gid, admin_home, &admin) != 0) {
      print_line("Erro ao montar registro do administrador.");
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    if (userdb_add(&admin) != 0) {
      print_line("Erro ao salvar usuario administrador.");
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    sync_root_device();

    struct user_record verify_rec;
    if (userdb_authenticate(admin_username, admin_password, &verify_rec) != 0) {
      print_line("Falha no teste de autenticacao do administrador. Recriando "
                 "base de usuarios.");
      vfs_unlink(USER_DB_PATH);
      if (userdb_ensure() != 0) {
        print_line("Nao foi possivel reconstruir /etc/users.db.");
        memory_zero(admin_password, sizeof(admin_password));
        return -1;
      }
      sync_root_device();
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    admin_uid = admin.uid;
    admin_gid = admin.gid;
    print_line("   Usuario administrador validado com sucesso.");
    memory_zero(admin_password, sizeof(admin_password));
    admin_ready = 1;
  }

  memory_zero(admin_password, sizeof(admin_password));
  log_process_conclude(proc_admin);
  log_process_finalize(proc_admin);
  log_process_finalize_success(proc_admin);

  log_process_progress(proc_settings);
  char config_buffer[256];
  memory_zero(config_buffer, sizeof(config_buffer));
  const char *splash_value = splash_enabled ? "enabled" : "disabled";
  const char *keyboard_value = keyboard_current_layout();
  if (!keyboard_value || !keyboard_value[0]) {
    keyboard_value = "us";
  }

  size_t idx = 0;
  const char *entries[] = {
      "hostname=", hostname,       "\n", "theme=",  theme,        "\n",
      "keyboard=", keyboard_value, "\n", "splash=", splash_value, "\n"};
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
    const char *p = entries[i];
    size_t plen = cstring_length(p);
    for (size_t j = 0; j < plen && idx < sizeof(config_buffer) - 1; ++j) {
      config_buffer[idx++] = p[j];
    }
  }
  config_buffer[idx] = '\0';

  const char *proc_config = "gravacao da configuracao do sistema";
  log_dependency_wait(proc_admin, proc_config);
  log_process_begin(proc_config);
  log_process_begin_success(proc_config);
  if (write_text_file("/system/config.ini", config_buffer) != 0) {
    print_line("Falha ao gravar configuracao inicial.");
    return -1;
  }
  sync_root_device();
  if (verify_config_file(hostname, theme, keyboard_value, splash_enabled) !=
      0) {
    return -1;
  }
  print_line("   /system/config.ini validado com sucesso.");

  if (system_mark_first_boot_complete() != 0) {
    print_line("Nao foi possivel registrar conclusao da configuracao.");
    return -1;
  }
  sync_root_device();
  log_process_conclude(proc_config);
  log_process_finalize(proc_config);
  log_process_finalize_success(proc_config);

  char uid_buf[12];
  char gid_buf[12];
  u32_to_string(admin_uid, uid_buf, sizeof(uid_buf));
  u32_to_string(admin_gid, gid_buf, sizeof(gid_buf));
  char admin_summary[160];
  admin_summary[0] = '\0';
  buffer_append(admin_summary, sizeof(admin_summary),
                "Administrador configurado: ");
  buffer_append(admin_summary, sizeof(admin_summary), admin_username);
  buffer_append(admin_summary, sizeof(admin_summary), " (UID ");
  buffer_append(admin_summary, sizeof(admin_summary), uid_buf);
  buffer_append(admin_summary, sizeof(admin_summary), ", GID ");
  buffer_append(admin_summary, sizeof(admin_summary), gid_buf);
  buffer_append(admin_summary, sizeof(admin_summary), ")");
  print_line(admin_summary);
  vga_newline();

  struct user_record final_rec;
  if (userdb_find(admin_username, &final_rec) == 0) {
    print_line("   Validacao final do registro admin concluida.");
    log_user_record_state(&final_rec);
  } else {
    print_line(
        "   Aviso: nao foi possivel reler registro admin apos configuracao.");
  }

  log_flush_pending();
  log_process_conclude(proc_settings);
  log_process_finalize(proc_settings);
  log_process_finalize_success(proc_settings);
  return 0;
}

/* API publica: versao sem senha pre-definida (pede ao usuario). */
int system_run_first_boot_setup(void) { return first_boot_setup_impl(); }

/* API publica: versao com senha pre-definida (reservada para uso futuro). */
int system_run_first_boot_setup_with_password(const char *admin_password) {
  (void)admin_password; /* Senhas de disco e usuario sao separadas por
                           seguranca. */
  return first_boot_setup_impl();
}

static void apply_config_line(struct system_settings *settings,
                              const char *line, size_t len) {
  size_t eq = 0;
  while (eq < len && line[eq] != '=') {
    ++eq;
  }
  if (eq == 0 || eq >= len) {
    return;
  }
  char key[16];
  char value[64];
  size_t klen = (eq < sizeof(key) - 1) ? eq : (sizeof(key) - 1);
  for (size_t i = 0; i < klen; ++i) {
    key[i] = line[i];
  }
  key[klen] = '\0';
  size_t vlen = len - eq - 1;
  if (vlen >= sizeof(value)) {
    vlen = sizeof(value) - 1;
  }
  for (size_t i = 0; i < vlen; ++i) {
    value[i] = line[eq + 1 + i];
  }
  value[vlen] = '\0';

  if (strings_equal(key, "hostname")) {
    cstring_copy(settings->hostname, sizeof(settings->hostname), value);
  } else if (strings_equal(key, "theme")) {
    cstring_copy(settings->theme, sizeof(settings->theme), value);
  } else if (strings_equal(key, "keyboard")) {
    cstring_copy(settings->keyboard_layout, sizeof(settings->keyboard_layout),
                 value);
  } else if (strings_equal(key, "splash")) {
    if (value[0] == 'd' || value[0] == 'D') {
      settings->splash_enabled = 0;
    } else {
      settings->splash_enabled = 1;
    }
  }
}

int system_load_settings(struct system_settings *out) {
  if (!out) {
    return -1;
  }
  system_settings_set_defaults(out);

  struct file *f = vfs_open("/system/config.ini", 0);
  if (!f) {
    return -1;
  }
  size_t size = 0;
  if (f->dentry && f->dentry->inode) {
    size = f->dentry->inode->size;
  }
  char *buffer = (char *)kalloc(size + 1);
  if (!buffer) {
    vfs_close(f);
    return -1;
  }
  long read = vfs_read(f, buffer, size);
  vfs_close(f);
  if (read < 0) {
    kfree(buffer);
    return -1;
  }
  buffer[read] = '\0';

  size_t start = 0;
  for (size_t i = 0; i <= (size_t)read; ++i) {
    if (i == (size_t)read || buffer[i] == '\n') {
      size_t len = i - start;
      if (len > 0) {
        apply_config_line(out, &buffer[start], len);
      }
      start = i + 1;
    }
  }
  kfree(buffer);
  return 0;
}

int system_save_settings(const struct system_settings *settings) {
  if (!settings) {
    return -1;
  }
  if (write_settings_file(settings) != 0) {
    return -1;
  }
  sync_root_device();
  if (verify_config_file(settings->hostname, settings->theme,
                         settings->keyboard_layout,
                         settings->splash_enabled) != 0) {
    return -1;
  }
  return 0;
}

int system_save_keyboard_layout(const char *layout) {
  if (!layout) {
    return -1;
  }
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  /* Tolerate missing config.ini: fallback to defaults and then override layout
   */
  system_load_settings(&settings);
  cstring_copy(settings.keyboard_layout, sizeof(settings.keyboard_layout),
               layout);
  return system_save_settings(&settings);
}

void system_apply_keyboard_layout(const struct system_settings *settings) {
  const char *layout = (settings && settings->keyboard_layout[0])
                           ? settings->keyboard_layout
                           : "us";
  if (keyboard_set_layout_by_name(layout) != 0) {
    vga_write(
        "[kbd] layout desconhecido em config.ini; revertendo para 'us'.\n");
    keyboard_set_layout_by_name("us");
  }
}

int system_login(struct session_context *session,
                 const struct system_settings *settings) {
  if (!session) {
    return -1;
  }
  const char *proc_login = "autenticacao de usuario";
  log_process_begin(proc_login);
  log_process_begin_success(proc_login);
  vga_newline();
  print_line("== CapyOS Login ==");
  if (settings) {
    char host_msg[128];
    host_msg[0] = '\0';
    buffer_append(host_msg, sizeof(host_msg), "Host: ");
    buffer_append(host_msg, sizeof(host_msg), settings->hostname);
    print_line(host_msg);
  }
  char username[USER_NAME_MAX];
  char password[TTY_BUFFER_MAX];
  struct user_record record;

  while (1) {
    memory_zero(username, sizeof(username));
    memory_zero(password, sizeof(password));
    log_process_progress(proc_login);
    size_t ulen = wizard_prompt("Usuario: ", username, sizeof(username), 0);
    size_t plen = wizard_prompt("Senha: ", password, sizeof(password), 1);
    if (ulen == 0 || plen == 0) {
      print_line("Credenciais obrigatorias.");
      continue;
    }
    char attempt_msg[160];
    attempt_msg[0] = '\0';
    buffer_append(attempt_msg, sizeof(attempt_msg),
                  "Login tentativa para usuario=");
    buffer_append(attempt_msg, sizeof(attempt_msg), username);
    log_event(attempt_msg);

    if (userdb_authenticate(username, password, &record) == 0) {
      memory_zero(password, sizeof(password));
      session_begin(session, &record);
      char welcome[160];
      welcome[0] = '\0';
      buffer_append(welcome, sizeof(welcome), "Bem-vindo, ");
      buffer_append(welcome, sizeof(welcome), record.username);
      buffer_append(welcome, sizeof(welcome), ".");
      print_line(welcome);
      vga_newline();
      char success_msg[160];
      success_msg[0] = '\0';
      buffer_append(success_msg, sizeof(success_msg),
                    "Login bem-sucedido: usuario=");
      buffer_append(success_msg, sizeof(success_msg), record.username);
      log_event(success_msg);
      log_process_conclude(proc_login);
      log_process_finalize(proc_login);
      log_process_finalize_success(proc_login);
      return 0;
    }
    memory_zero(password, sizeof(password));
    print_line("Usuario ou senha invalidos.");
    char fail_msg[160];
    fail_msg[0] = '\0';
    buffer_append(fail_msg, sizeof(fail_msg), "Login falhou: usuario=");
    buffer_append(fail_msg, sizeof(fail_msg), username);
    log_event(fail_msg);
  }
}

void system_apply_theme(const struct system_settings *settings) {
  const char *theme = settings ? settings->theme : NULL;
  if (!theme) {
    vga_set_color(7, 0);
    return;
  }
  if (strings_equal(theme, "ocean")) {
    vga_set_color(11, 1); // cyan on blue
  } else if (strings_equal(theme, "forest")) {
    vga_set_color(10, 2); // green on greenish
  } else {
    vga_set_color(7, 0); // default noir
  }
}

void system_show_splash(const struct system_settings *settings) {
  if (!settings || !settings->splash_enabled) {
    return;
  }
  static const char *frames[] = {"[=         ]", "[===       ]", "[======    ]",
                                 "[========= ]", "[==========]"};
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
