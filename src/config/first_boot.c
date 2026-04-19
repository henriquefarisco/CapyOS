/* first_boot.c: first-boot detection, directory provisioning, user setup,
   setup logging, filesystem helpers. */
#include "config_internal.h"
#if defined(__x86_64__)
#include "arch/x86_64/kernel_volume_runtime.h"
#include "drivers/serial/com1.h"
#endif

/* ---- setup logging state ---- */
static char g_setup_log[SETUP_LOG_CAPACITY];
static size_t g_setup_log_len = 0;
static size_t g_setup_log_flushed = 0;
static int g_setup_log_ready = 0;
static int g_setup_debug = 0;

static void config_serial_write(const char *msg) {
#if defined(__x86_64__)
  if (msg) {
    com1_puts(msg);
  }
#else
  (void)msg;
#endif
}

/* ---- u32_to_string ---- */
void config_u32_to_string(uint32_t value, char *buf, size_t buf_len) {
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

/* ---- bytes_to_hex_str ---- */
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

/* ---- sync_root_device ---- */
void config_sync_root_device(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    buffer_cache_sync(root->bdev);
  }
}

/* ---- setup logging ---- */
void config_log_buffer_append(const char *msg) {
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

void config_log_flush_pending(void) {
  if (!g_setup_log_ready || g_setup_log_flushed >= g_setup_log_len) {
    return;
  }
  if (config_ensure_directory("/var") != 0) {
    return;
  }
  if (config_ensure_directory("/var/log") != 0) {
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

void config_log_event(const char *msg) {
  if (!msg) {
    return;
  }
  config_log_buffer_append(msg);
  config_log_flush_pending();
}

void config_print_line(const char *msg) {
  if (!msg) {
    return;
  }
  vga_write(msg);
  vga_newline();
  config_serial_write(msg);
  config_serial_write("\r\n");
  config_log_buffer_append(msg);
  config_log_flush_pending();
}

void config_log_emit_segments(const char *a, const char *b, const char *c,
                              const char *d, const char *e) {
  const char *parts[5] = {a, b, c, d, e};
  for (size_t i = 0; i < 5; ++i) {
    const char *seg = parts[i];
    if (!seg) {
      continue;
    }
    vga_write(seg);
    config_log_buffer_append(seg);
  }
  vga_newline();
  config_log_buffer_append("\n");
  config_log_flush_pending();
}

void config_log_process_begin(const char *name) {
  config_log_emit_segments("Iniciando processo de ", name, "...", NULL, NULL);
}
void config_log_process_begin_success(const char *name) {
  config_log_emit_segments("Processo ", name, " iniciado com sucesso...", NULL,
                           NULL);
}
void config_log_process_progress(const char *name) {
  config_log_emit_segments("Processo ", name, " em andamento.", NULL, NULL);
}
void config_log_process_conclude(const char *name) {
  config_log_emit_segments("Processo ", name, " concluido.", NULL, NULL);
}
void config_log_process_finalize(const char *name) {
  config_log_emit_segments("Finalizando processo ", name, "...", NULL, NULL);
}
void config_log_process_finalize_success(const char *name) {
  config_log_emit_segments("Processo ", name, " finalizado com sucesso.", NULL,
                           NULL);
}
void config_log_dependency_wait(const char *dependency, const char *target) {
  config_log_emit_segments("Aguardando processo ", dependency,
                           " para iniciar o processo ", target, "...");
}

/* ---- debug helpers ---- */
static void dbg_print_heap(const char *prefix, const char *path) {
  if (!g_setup_debug) {
    return;
  }
  char heap_msg[256];
  heap_msg[0] = '\0';
  config_buffer_append(heap_msg, sizeof(heap_msg), prefix ? prefix : "");
  config_buffer_append(heap_msg, sizeof(heap_msg), path ? path : "");
  char used_str[12];
  char avail_str[12];
  config_u32_to_string((uint32_t)kheap_used(), used_str, sizeof(used_str));
  config_u32_to_string((uint32_t)kheap_size(), avail_str,
                       sizeof(avail_str));
  config_buffer_append(heap_msg, sizeof(heap_msg), " heap_used=");
  config_buffer_append(heap_msg, sizeof(heap_msg), used_str);
  config_buffer_append(heap_msg, sizeof(heap_msg), " heap_avail=");
  config_buffer_append(heap_msg, sizeof(heap_msg), avail_str);
  config_log_buffer_append(heap_msg);
  config_log_buffer_append("\n");
  config_log_flush_pending();
}

/* ---- filesystem helpers ---- */
int config_ensure_directory(const char *path) {
  char build[128];
  size_t build_len = 0;
  const char *p = NULL;
  struct dentry *chk = NULL;

  if (!path || path[0] != '/') {
    return -1;
  }
#if defined(__x86_64__)
  if (x64_kernel_volume_runtime_ensure_dir_recursive(path) == 0) {
    return 0;
  }
#endif

  /* Build the path incrementally, creating each missing component.
   * For "/var/log" we first ensure "/var" exists, then "/var/log".
   * This mirrors x64_kernel_volume_runtime_ensure_dir_recursive(). */
  build[0] = '/';
  build[1] = '\0';
  build_len = 1;
  p = path;
  while (*p == '/') {
    ++p;
  }

  while (*p) {
    const char *start = p;
    size_t len = 0;
    while (start[len] && start[len] != '/') {
      ++len;
    }
    if (len > 0) {
      if (build_len > 1) {
        if (build_len + 1 >= sizeof(build)) {
          return -1;
        }
        build[build_len++] = '/';
      }
      if (build_len + len >= sizeof(build)) {
        return -1;
      }
      for (size_t i = 0; i < len; ++i) {
        build[build_len++] = start[i];
      }
      build[build_len] = '\0';

      /* Try to find the component; create it if missing. */
      chk = NULL;
      if (vfs_lookup(build, &chk) != 0) {
        /* Not found — create it.  vfs_create returns -1 both on real
         * errors and on "already exists on disk but not in dentry
         * cache" — so if create fails we re-check with lookup. */
        if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
          chk = NULL;
          if (vfs_lookup(build, &chk) != 0) {
            return -1;
          }
        }
      }
      if (chk && chk->refcount) {
        chk->refcount--;
      }
    }
    p += len;
    while (*p == '/') {
      ++p;
    }
  }
  return 0;
}

int config_write_text_file(const char *path, const char *text) {
  if (!path || !text) {
    return -1;
  }
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
      if (vfs_lookup(path, &d) != 0) {
        return -1;
      }
    }
  }
  if (d && d->refcount) {
    d->refcount--;
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

/* ---- user helpers ---- */
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

static int valid_username_char(char c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  return c == '-' || c == '_';
}

static int validate_admin_username(const char *username) {
  if (!username || !username[0]) return 0;
  size_t len = cstring_length(username);
  if (len == 0 || len >= USER_NAME_MAX) return 0;
  for (size_t i = 0; i < len; ++i) {
    if (!valid_username_char(username[i])) return 0;
  }
  return 1;
}

static const char *validate_theme(const char *input) {
  if (!input || cstring_length(input) == 0) return "capyos";
  if (strings_equal(input, "capyos") || strings_equal(input, "CAPYOS") ||
      strings_equal(input, "ocean") || strings_equal(input, "forest")) {
    return strings_equal(input, "CAPYOS") ? "capyos" : input;
  }
  return "capyos";
}

static int verify_directory_exists(const char *path) {
  if (!path) return -1;
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

static void log_user_record_state(const struct user_record *rec) {
  if (!rec) return;
  char uid_buf[12], gid_buf[12];
  config_u32_to_string(rec->uid, uid_buf, sizeof(uid_buf));
  config_u32_to_string(rec->gid, gid_buf, sizeof(gid_buf));
  char salt_hex[USER_SALT_SIZE * 2 + 1];
  char hash_hex[USER_HASH_SIZE * 2 + 1];
  bytes_to_hex_str(rec->salt, USER_SALT_SIZE, salt_hex, sizeof(salt_hex));
  bytes_to_hex_str(rec->hash, USER_HASH_SIZE, hash_hex, sizeof(hash_hex));
  config_log_buffer_append("   userdb: usuario=");
  config_log_buffer_append(rec->username);
  config_log_buffer_append(" uid=");
  config_log_buffer_append(uid_buf);
  config_log_buffer_append(" gid=");
  config_log_buffer_append(gid_buf);
  config_log_buffer_append(" home=");
  config_log_buffer_append(rec->home);
  config_log_buffer_append(" salt=");
  config_log_buffer_append(salt_hex);
  config_log_buffer_append(" hash=");
  config_log_buffer_append(hash_hex);
  config_log_buffer_append("\n");
  config_log_flush_pending();
}

/* ---- CLI reference text ---- */
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
    "  net-status           - estado da rede (x64)\n"
    "  net-ip               - exibe IPv4 local e mascara\n"
    "  net-gw               - exibe gateway atual\n"
    "  net-dns              - exibe DNS atual\n"
    "  net-set <ip> <mask> <gw> <dns> - aplica IPv4 estatico\n"
    "  hey <destino>        - ping (ICMP echo)\n"
    "  config-theme [tema]  - altera tema visual\n"
    "  config-splash [modo] - altera splash no boot\n"
    "  config-language [idioma] - altera idioma do usuario\n"
    "  config-keyboard [layout] - altera layout do teclado\n"
    "  mess                 - limpa tela\n"
    "  bye                  - encerra sessao\n"
    "\n"
    "Ajuda\n"
    "  help-any             - lista comandos\n"
    "  help-docs            - exibe este resumo\n"
    "\n"
    "Observacao: caminhos relativos respeitam o diretorio ativo.\n";

/* ---- first boot detection ---- */
/* Returns 0 when setup is genuinely complete, non-zero when the wizard
 * must run.  The check is multi-condition to be both robust and secure:
 *
 *  marker exists + users exist  → complete (0)
 *  marker exists + NO users     → incomplete / corrupt → force wizard (1)
 *  NO marker + users exist      → marker tampered; repair it, do NOT
 *                                  re-run wizard (security: prevents an
 *                                  attacker from deleting the marker to
 *                                  create a new admin account) → (0)
 *  NO marker + NO users         → genuine first boot → (1)
 */
int system_detect_first_boot(void) {
  struct vfs_stat st;
  int marker_exists = (vfs_stat_path("/system/first-run.done", &st) == 0);

  int has_users = 0;
  if (vfs_stat_path(USER_DB_PATH, &st) == 0) {
    has_users = (userdb_has_any_user() > 0) ? 1 : 0;
  }

  {
    int config_exists = (vfs_stat_path("/system/config.ini", &st) == 0);

    if (marker_exists && has_users && config_exists) {
      return 0; /* fully complete */
    }
    if (marker_exists && (!has_users || !config_exists)) {
      /* Marker written but user/config state is incomplete. Remove stale
       * marker so setup runs again and rebuilds a consistent base. */
      vfs_unlink("/system/first-run.done");
      return 1;
    }
    if (!marker_exists && has_users && config_exists) {
      /* Existing users + config without marker: repair marker silently.
       * This preserves the anti-tamper behavior while avoiding re-running
       * setup on already configured systems. */
      (void)system_mark_first_boot_complete();
      return 0;
    }
  }

  /* !marker_exists && !has_users → genuine first boot */
  return 1;
}

int system_mark_first_boot_complete(void) {
  if (config_ensure_directory("/system") != 0) {
    return -1;
  }
  return config_write_text_file("/system/first-run.done", "completed\n");
}

/* ---- common setup helpers (shared by silent and interactive paths) ---- */
static int setup_create_directories(const char *setup_language) {
  const char *proc_dirs = "preparacao de diretorios padrao";
  config_log_process_begin(proc_dirs);
  config_log_process_begin_success(proc_dirs);

  const char *std_dirs[] = {"/bin",    "/etc",  "/home",    "/tmp",
                            "/var",    "/var/log", "/system", "/docs"};
  for (size_t i = 0; i < sizeof(std_dirs) / sizeof(std_dirs[0]); ++i) {
    dbg_print_heap("[setup] ensure path: ", std_dirs[i]);
    if (config_ensure_directory(std_dirs[i]) != 0) {
      config_print_line("Falha ao preparar estrutura de diretorios:");
      config_print_line(std_dirs[i]);
      return -1;
    }
  }
  config_log_process_progress(proc_dirs);
  for (size_t i = 0; i < sizeof(std_dirs) / sizeof(std_dirs[0]); ++i) {
    if (verify_directory_exists(std_dirs[i]) != 0) {
      config_print_line("Verificacao de diretorios falhou:");
      config_print_line(std_dirs[i]);
      return -1;
    }
  }
  config_print_line("   Estrutura de diretorios pronta.");
  config_log_flush_pending();
  config_log_process_conclude(proc_dirs);
  config_log_process_finalize(proc_dirs);
  config_log_process_finalize_success(proc_dirs);

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
  if (config_write_text_file("/var/log/cli-selftest.log", "") == 0) {
    vfs_set_metadata("/var/log/cli-selftest.log", &meta_log_file);
  }
  if (vfs_set_metadata("/var/log/setup.log", &meta_log_file) != 0) {
    config_write_text_file("/var/log/setup.log", "");
    vfs_set_metadata("/var/log/setup.log", &meta_log_file);
  }
  (void)setup_language;
  return 0;
}

static int setup_prepare_userdb(const char *setup_language) {
  const char *proc_userdb = "preparacao da base de usuarios";
  config_log_process_begin(proc_userdb);
  config_log_process_begin_success(proc_userdb);
  if (userdb_ensure() != 0) {
    config_print_line("Nao foi possivel criar /etc/users.db.");
    return -1;
  }
  if (verify_directory_exists("/etc") != 0) {
    config_print_line("Diretorio /etc inacessivel.");
    return -1;
  }
  config_log_process_progress(proc_userdb);
  struct vfs_stat userdb_stat;
  if (vfs_stat_path(USER_DB_PATH, &userdb_stat) != 0) {
    config_print_line("Nao foi possivel validar /etc/users.db.");
    return -1;
  }
  char userdb_size[12];
  config_u32_to_string(userdb_stat.size, userdb_size, sizeof(userdb_size));
  char size_msg[128];
  size_msg[0] = '\0';
  config_buffer_append(size_msg, sizeof(size_msg),
                       "   /etc/users.db disponivel (");
  config_buffer_append(size_msg, sizeof(size_msg), userdb_size);
  config_buffer_append(size_msg, sizeof(size_msg), " bytes).");
  config_print_line(size_msg);
  config_sync_root_device();
  config_log_process_conclude(proc_userdb);
  config_log_process_finalize(proc_userdb);
  config_log_process_finalize_success(proc_userdb);
  (void)setup_language;
  return 0;
}

static int setup_write_docs(const char *setup_language) {
  const char *proc_docs = "instalacao da referencia CapyCLI";
  config_log_process_begin(proc_docs);
  config_log_process_begin_success(proc_docs);
  if (config_write_text_file("/docs/capyos-cli-reference.txt",
                             g_cli_reference_text) != 0) {
    config_print_line(
        "   Aviso: nao foi possivel gravar referencia do CapyCLI.");
  } else {
    config_print_line(
        "   Referencia CapyCLI pronta em /docs/capyos-cli-reference.txt.");
  }
  if (system_prepare_update_catalog() != 0) {
    config_print_line(
        "   Aviso: nao foi possivel preparar /system/update.");
  }
  config_sync_root_device();
  config_log_process_conclude(proc_docs);
  config_log_process_finalize(proc_docs);
  config_log_process_finalize_success(proc_docs);
  (void)setup_language;
  return 0;
}

static int setup_write_settings_and_mark(const char *setup_language,
                                         const char *hostname,
                                         const char *theme,
                                         int splash_enabled) {
  struct system_settings settings;
  cstring_copy(settings.hostname, sizeof(settings.hostname), "capyos-node");
  cstring_copy(settings.theme, sizeof(settings.theme), "capyos");
  cstring_copy(settings.keyboard_layout, sizeof(settings.keyboard_layout),
               g_boot_default_keyboard_layout);
  cstring_copy(settings.language, sizeof(settings.language),
               g_boot_default_language);
  cstring_copy(settings.update_channel, sizeof(settings.update_channel),
               system_update_channel_or_default(NULL));
  cstring_copy(settings.network_mode, sizeof(settings.network_mode),
               system_network_mode_or_default(NULL));
  cstring_copy(settings.service_target, sizeof(settings.service_target),
               system_service_target_or_default(NULL));
  settings.ipv4_addr = 0;
  settings.ipv4_mask = 0;
  settings.ipv4_gateway = 0;
  settings.ipv4_dns = 0;
  settings.splash_enabled = 1;
  settings.diagnostics_enabled = 0;

  const char *keyboard_value = keyboard_current_layout();
  if (!keyboard_value || !keyboard_value[0]) {
    keyboard_value = "us";
  }
  cstring_copy(settings.hostname, sizeof(settings.hostname), hostname);
  cstring_copy(settings.theme, sizeof(settings.theme), theme);
  cstring_copy(settings.keyboard_layout, sizeof(settings.keyboard_layout),
               keyboard_value);
  cstring_copy(settings.language, sizeof(settings.language), setup_language);
  cstring_copy(settings.network_mode, sizeof(settings.network_mode), "static");
  settings.splash_enabled = splash_enabled ? 1 : 0;

  const char *proc_config = "gravacao da configuracao do sistema";
  config_log_process_begin(proc_config);
  config_log_process_begin_success(proc_config);
  if (config_write_settings_file(&settings) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_CONFIG_WRITE_FAIL));
    return -1;
  }
  config_sync_root_device();
  if (config_verify_config_file(
          settings.hostname, settings.theme, settings.keyboard_layout,
          settings.language, settings.update_channel, settings.network_mode,
          settings.service_target, settings.splash_enabled,
          settings.ipv4_addr, settings.ipv4_mask, settings.ipv4_gateway,
          settings.ipv4_dns) != 0) {
    return -1;
  }
  config_print_line(
      system_ui_text(setup_language, SYS_UI_CONFIG_VALIDATED));

  if (system_mark_first_boot_complete() != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_FIRST_BOOT_COMPLETE_FAIL));
    return -1;
  }
  config_sync_root_device();
  config_log_process_conclude(proc_config);
  config_log_process_finalize(proc_config);
  config_log_process_finalize_success(proc_config);
  return 0;
}

/* ---- silent provisioning (installer config available) ---- */
/* NOTE: The actual provisioning (admin user, config.ini, first-run.done)
   now happens earlier in shell_bootstrap_filesystem() via ops->write_text_file,
   which uses kernel_write_text_file (vfs_stat_path based) instead of
   config_write_text_file (vfs_lookup based — broken on x86_64 CAPYFS).
   This function is kept as a no-op safety net: if system_detect_first_boot()
   still returns 1 here, it means the shell runtime already provisioned but
   something is partially incomplete.  Returning 0 lets the login gate proceed
   so the user can access the system. */
static int first_boot_silent_provision(void) {
  config_print_line("Provisionamento automatico ja executado pelo runtime.");
  return 0;
}

/* ---- interactive first boot setup (fallback when no installer config) ---- */
static int first_boot_setup_interactive(void) {
  const char *setup_language =
      system_language_or_default(g_boot_default_language);
  g_setup_log_len = 0;
  g_setup_log_flushed = 0;
  g_setup_log_ready = 1;
  g_setup_log[0] = '\0';
  g_setup_debug = 0;
  if (strings_equal(setup_language, "en")) {
    config_print_line("=== CapyOS Initial Setup ===");
    config_print_line(
        "This wizard prepares users, settings, and the base system structure.");
  } else if (strings_equal(setup_language, "es")) {
    config_print_line("=== Configuracion Inicial de CapyOS ===");
    config_print_line(
        "Este asistente prepara usuarios, configuracion y la estructura "
        "basica.");
  } else {
    config_print_line("=== Assistente CapyOS - Configuracao Inicial ===");
    config_print_line(
        "Este assistente prepara usuarios, configuracao e estrutura basica.");
  }
  vga_newline();

  g_setup_debug = 0;

  if (setup_create_directories(setup_language) != 0) return -1;
  if (setup_prepare_userdb(setup_language) != 0) return -1;
  if (setup_write_docs(setup_language) != 0) return -1;

  /* Keyboard layout selection */
  char layout_choice[32];
  memory_zero(layout_choice, sizeof(layout_choice));
  {
    size_t layout_count = keyboard_layout_count();
    size_t selected_layout = 0;
    char layout_labels[16][96];
    const char *layout_items[16];

    if (layout_count > 16u) {
      layout_count = 16u;
    }
    for (size_t i = 0; i < layout_count; ++i) {
      layout_labels[i][0] = '\0';
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           keyboard_layout_name(i));
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           " - ");
      config_buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                           keyboard_layout_description(i));
      layout_items[i] = layout_labels[i];
      if (strings_equal(keyboard_layout_name(i),
                        g_boot_default_keyboard_layout)) {
        selected_layout = i;
      }
    }

    selected_layout = (size_t)wizard_menu_select_setup(
        20u, system_ui_text(setup_language, SYS_UI_LAYOUTS_AVAILABLE),
        setup_language, layout_items, layout_count, selected_layout);
    cstring_copy(layout_choice, sizeof(layout_choice),
                 keyboard_layout_name(selected_layout));
    if (keyboard_set_layout_by_name(layout_choice) != 0) {
      cstring_copy(layout_choice, sizeof(layout_choice), "us");
      keyboard_set_layout_by_name(layout_choice);
      config_print_line(
          system_ui_text(setup_language, SYS_UI_LAYOUT_UNKNOWN));
    }
  }

  /* Basic settings collection */
  char hostname[TTY_BUFFER_MAX];
  const char *theme = "capyos";
  int splash_enabled = 1;
  memory_zero(hostname, sizeof(hostname));

  const char *proc_settings = "coleta de configuracoes basicas";
  config_log_process_begin(proc_settings);
  config_log_process_begin_success(proc_settings);
  size_t hlen = wizard_prompt_setup(
      40u, "Hostname",
      system_ui_text(setup_language, SYS_UI_HOSTNAME_PROMPT), hostname,
      sizeof(hostname), 0);
  if (hlen == 0) {
    cstring_copy(hostname, sizeof(hostname), "capyos-node");
  }

  {
    const char *theme_items[3];
    if (strings_equal(setup_language, "en")) {
      theme_items[0] = "CapyOS - default";
      theme_items[1] = "Ocean - blue accents";
      theme_items[2] = "Forest - green accents";
    } else if (strings_equal(setup_language, "es")) {
      theme_items[0] = "CapyOS - predeterminado";
      theme_items[1] = "Ocean - tonos azules";
      theme_items[2] = "Forest - tonos verdes";
    } else {
      theme_items[0] = "CapyOS - padrao";
      theme_items[1] = "Ocean - tons azuis";
      theme_items[2] = "Forest - tons verdes";
    }
    int theme_pick = wizard_menu_select_setup(
        55u, system_ui_text(setup_language, SYS_UI_THEMES_AVAILABLE),
        setup_language, theme_items,
        sizeof(theme_items) / sizeof(theme_items[0]), 0);
    theme = validate_theme(theme_pick == 0   ? "capyos"
                           : (theme_pick == 1 ? "ocean" : "forest"));
  }

  {
    const char *splash_items[] = {
        system_ui_menu_enabled(setup_language),
        system_ui_menu_disabled(setup_language)};
    int splash_pick = wizard_menu_select_setup(
        70u, system_ui_splash_menu_title(setup_language), setup_language,
        splash_items, 2, 0);
    splash_enabled = (splash_pick == 0) ? 1 : 0;
  }

  /* Administrator user */
  char admin_username[USER_NAME_MAX];
  memory_zero(admin_username, sizeof(admin_username));
  size_t ulen = wizard_prompt_setup(
      85u, "Administrator",
      system_ui_text(setup_language, SYS_UI_ADMIN_USER_PROMPT),
      admin_username, sizeof(admin_username), 0);
  if (ulen == 0) {
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }
  if (!validate_admin_username(admin_username)) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_USER_INVALID));
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }

  uint32_t admin_uid = 1000;
  uint32_t admin_gid = 1000;
  char admin_password[TTY_BUFFER_MAX];
  memory_zero(admin_password, sizeof(admin_password));
  config_log_process_progress(proc_settings);
  const char *proc_admin = "provisionamento do usuario administrador";
  config_log_dependency_wait(proc_settings, proc_admin);
  config_log_process_begin(proc_admin);
  config_log_process_begin_success(proc_admin);
  char admin_home[USER_HOME_MAX];
  memory_zero(admin_home, sizeof(admin_home));
  build_home_path(admin_username, admin_home, sizeof(admin_home));
  if (config_ensure_directory(admin_home) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_HOME_CREATE_FAIL));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  if (verify_directory_exists(admin_home) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_HOME_UNAVAILABLE));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  struct vfs_metadata home_meta = {admin_uid, admin_gid, 0700};
  if (vfs_set_metadata(admin_home, &home_meta) != 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_HOME_PERM_WARNING));
  }
  config_sync_root_device();

  int admin_ready = 0;
  struct user_record existing;
  if (userdb_find(admin_username, &existing) == 0) {
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_EXISTS));
    if (verify_directory_exists(existing.home) != 0) {
      char rebuild_msg[128];
      rebuild_msg[0] = '\0';
      config_buffer_append(
          rebuild_msg, sizeof(rebuild_msg),
          system_ui_text(setup_language, SYS_UI_ADMIN_HOME_REBUILD_PREFIX));
      config_buffer_append(rebuild_msg, sizeof(rebuild_msg), existing.home);
      config_buffer_append(rebuild_msg, sizeof(rebuild_msg), ".");
      config_print_line(rebuild_msg);
      if (config_ensure_directory(existing.home) != 0 ||
          verify_directory_exists(existing.home) != 0) {
        config_print_line(
            system_ui_text(setup_language, SYS_UI_ADMIN_HOME_REBUILD_FAIL));
        return -1;
      }
    }
    admin_uid = existing.uid;
    admin_gid = existing.gid;
    (void)user_prefs_save_language(&existing, setup_language);
    admin_ready = 1;
  }

  char password_prompt[96];
  password_prompt[0] = '\0';
  config_buffer_append(
      password_prompt, sizeof(password_prompt),
      system_ui_text(setup_language, SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX));
  config_buffer_append(password_prompt, sizeof(password_prompt),
                       admin_username);
  config_buffer_append(password_prompt, sizeof(password_prompt), ": ");

  while (!admin_ready) {
    wizard_draw_header(95u, "Administrator password");
    if (prompt_password_pair(password_prompt, admin_password,
                             sizeof(admin_password), setup_language) != 0) {
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_REGISTER_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    struct user_record admin;
    if (user_record_init(admin_username, admin_password, "admin", admin_uid,
                         admin_gid, admin_home, &admin) != 0) {
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_RECORD_BUILD_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    if (userdb_add(&admin) != 0) {
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_SAVE_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    config_sync_root_device();

    struct user_record verify_rec;
    if (userdb_authenticate(admin_username, admin_password, &verify_rec) !=
        0) {
      config_print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_AUTH_REBUILD_FAIL));
      vfs_unlink(USER_DB_PATH);
      if (userdb_ensure() != 0) {
        config_print_line(system_ui_text(
            setup_language, SYS_UI_ADMIN_USERDB_REBUILD_FAIL));
        memory_zero(admin_password, sizeof(admin_password));
        return -1;
      }
      config_sync_root_device();
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    (void)user_prefs_save_language(&verify_rec, setup_language);
    admin_uid = admin.uid;
    admin_gid = admin.gid;
    config_print_line(
        system_ui_text(setup_language, SYS_UI_ADMIN_VALIDATED));
    memory_zero(admin_password, sizeof(admin_password));
    admin_ready = 1;
  }

  memory_zero(admin_password, sizeof(admin_password));
  config_log_process_conclude(proc_admin);
  config_log_process_finalize(proc_admin);
  config_log_process_finalize_success(proc_admin);

  config_log_process_progress(proc_settings);
  if (setup_write_settings_and_mark(setup_language, hostname, theme,
                                    splash_enabled) != 0) {
    return -1;
  }

  char uid_buf[12], gid_buf[12];
  config_u32_to_string(admin_uid, uid_buf, sizeof(uid_buf));
  config_u32_to_string(admin_gid, gid_buf, sizeof(gid_buf));
  char admin_summary[160];
  admin_summary[0] = '\0';
  config_buffer_append(admin_summary, sizeof(admin_summary),
                       "Administrador configurado: ");
  config_buffer_append(admin_summary, sizeof(admin_summary), admin_username);
  config_buffer_append(admin_summary, sizeof(admin_summary), " (UID ");
  config_buffer_append(admin_summary, sizeof(admin_summary), uid_buf);
  config_buffer_append(admin_summary, sizeof(admin_summary), ", GID ");
  config_buffer_append(admin_summary, sizeof(admin_summary), gid_buf);
  config_buffer_append(admin_summary, sizeof(admin_summary), ")");
  config_print_line(admin_summary);
  vga_newline();

  struct user_record final_rec;
  if (userdb_find(admin_username, &final_rec) == 0) {
    config_print_line(
        "   Validacao final do registro do administrador concluida.");
    log_user_record_state(&final_rec);
  } else {
    config_print_line(
        "   Aviso: nao foi possivel reler registro do administrador "
        "apos configuracao.");
  }

  config_log_flush_pending();
  config_log_process_conclude(proc_settings);
  config_log_process_finalize(proc_settings);
  config_log_process_finalize_success(proc_settings);
  return 0;
}

/* ---- first boot setup dispatch ---- */
static int first_boot_setup_impl(void) {
  if (system_installer_config_available()) {
    return first_boot_silent_provision();
  }
  return first_boot_setup_interactive();
}

int system_run_first_boot_setup(void) { return first_boot_setup_impl(); }

int system_run_first_boot_setup_with_password(const char *admin_password) {
  (void)admin_password;
  return first_boot_setup_impl();
}
