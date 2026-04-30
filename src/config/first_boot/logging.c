#include "../internal/first_boot_internal.h"

#if defined(__x86_64__)
#include "drivers/serial/serial_com1.h"
#endif

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

void config_sync_root_device(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    buffer_cache_sync(root->bdev);
  }
}

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

  {
    struct dentry *d = NULL;
    if (vfs_lookup("/var/log/setup.log", &d) != 0) {
      if (vfs_create("/var/log/setup.log", VFS_MODE_FILE, NULL) != 0) {
        return;
      }
    } else if (d && d->refcount) {
      d->refcount--;
    }
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

void config_debug_print_heap(const char *prefix, const char *path) {
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

void config_first_boot_log_reset(void) {
  g_setup_log_len = 0;
  g_setup_log_flushed = 0;
  g_setup_log_ready = 1;
  g_setup_log[0] = '\0';
  g_setup_debug = 0;
}
