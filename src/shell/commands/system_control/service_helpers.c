#include "internal/system_control_internal.h"

void sync_and_flush(void) {
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

void update_history_append_event(const char *event_name,
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
  append_text(line, sizeof(line), " channel=");
  append_text(line, sizeof(line), status->channel[0] ? status->channel : "stable");
  append_text(line, sizeof(line), " branch=");
  append_text(line, sizeof(line), status->branch[0] ? status->branch : "main");
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

int shell_recovery_capyfs_check(struct capyfs_check_report *out) {
  struct super_block *root = vfs_root();
  if (!root || !root->bdev || !out) {
    return -1;
  }
  return capyfs_check(root->bdev, out);
}

#if defined(__x86_64__)
int recovery_storage_ensure_base_layout(void) {
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

int recovery_storage_rewrite_config(const struct shell_context *ctx) {
  struct system_settings settings;

  if (ctx && ctx->settings) {
    settings = *ctx->settings;
  } else if (system_load_settings(&settings) != 0) {
    /* system_load_settings() already populates defaults on failure. */
  }

  return system_save_settings(&settings);
}

int recovery_storage_reset_admin(const char *password) {
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

int cmd_service_control(struct shell_context *ctx, int argc, char **argv) {
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
