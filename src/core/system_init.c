/* system_init.c: system setup helpers (config, first-run, users, theme). */
#include "core/system_init.h"

#include "core/localization.h"
#include "core/service_manager.h"
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

static char g_boot_default_keyboard_layout[16] = "us";
static char g_boot_default_language[16] = "en";
static struct system_runtime_platform g_runtime_platform = {0};

static const char *normalize_keyboard_layout_name(const char *input) {
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

static const char *system_language_or_default(const char *language) {
  const char *normalized = localization_normalize_language(language);
  return normalized ? normalized : "en";
}

static const char *system_network_mode_or_default(const char *mode) {
  if (mode && strings_equal(mode, "dhcp")) {
    return "dhcp";
  }
  return "static";
}

static const char *system_service_target_or_default(const char *target) {
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

static void system_ipv4_to_string(uint32_t ip, char out[16]) {
  char *p = out;
  uint8_t octets[4];
  if (!out) {
    return;
  }
  octets[0] = (uint8_t)((ip >> 24) & 0xFFu);
  octets[1] = (uint8_t)((ip >> 16) & 0xFFu);
  octets[2] = (uint8_t)((ip >> 8) & 0xFFu);
  octets[3] = (uint8_t)(ip & 0xFFu);
  for (uint32_t i = 0; i < 4; ++i) {
    uint8_t value = octets[i];
    if (value >= 100u) {
      *p++ = (char)('0' + (value / 100u));
      value %= 100u;
      *p++ = (char)('0' + (value / 10u));
      *p++ = (char)('0' + (value % 10u));
    } else if (value >= 10u) {
      *p++ = (char)('0' + (value / 10u));
      *p++ = (char)('0' + (value % 10u));
    } else {
      *p++ = (char)('0' + value);
    }
    if (i != 3u) {
      *p++ = '.';
    }
  }
  *p = '\0';
}

static int system_parse_ipv4(const char *text, uint32_t *out) {
  uint32_t parts[4] = {0, 0, 0, 0};
  uint32_t idx = 0;
  uint32_t value = 0;
  int has_digit = 0;
  const char *p = text;

  if (!text || !out) {
    return -1;
  }
  for (;; ++p) {
    char ch = *p;
    if (ch >= '0' && ch <= '9') {
      has_digit = 1;
      value = (value * 10u) + (uint32_t)(ch - '0');
      if (value > 255u) {
        return -1;
      }
      continue;
    }
    if (ch == '.' || ch == '\0') {
      if (!has_digit || idx >= 4u) {
        return -1;
      }
      parts[idx++] = value;
      value = 0;
      has_digit = 0;
      if (ch == '\0') {
        break;
      }
      continue;
    }
    return -1;
  }
  if (idx != 4u) {
    return -1;
  }
  *out = system_ipv4_addr((uint8_t)parts[0], (uint8_t)parts[1],
                          (uint8_t)parts[2], (uint8_t)parts[3]);
  return 0;
}

void system_set_boot_defaults(const char *keyboard_layout, const char *language) {
  cstring_copy(g_boot_default_keyboard_layout,
               sizeof(g_boot_default_keyboard_layout),
               normalize_keyboard_layout_name(keyboard_layout));
  cstring_copy(g_boot_default_language, sizeof(g_boot_default_language),
               system_language_or_default(language));
}

void system_runtime_platform_set(const struct system_runtime_platform *status) {
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

static void system_settings_set_defaults(struct system_settings *settings) {
  if (!settings) {
    return;
  }
  cstring_copy(settings->hostname, sizeof(settings->hostname), "capyos-node");
  cstring_copy(settings->theme, sizeof(settings->theme), "capyos");
  cstring_copy(settings->keyboard_layout, sizeof(settings->keyboard_layout),
               g_boot_default_keyboard_layout);
  cstring_copy(settings->language, sizeof(settings->language),
               g_boot_default_language);
  cstring_copy(settings->network_mode, sizeof(settings->network_mode),
               "static");
  cstring_copy(settings->service_target, sizeof(settings->service_target),
               system_service_target_or_default(NULL));
  settings->ipv4_addr = system_ipv4_addr(10, 0, 2, 15);
  settings->ipv4_mask = system_ipv4_addr(255, 255, 255, 0);
  settings->ipv4_gateway = system_ipv4_addr(10, 0, 2, 2);
  settings->ipv4_dns = system_ipv4_addr(1, 1, 1, 1);
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

int system_prepare_update_catalog(void) {
  static const char *repo_defaults =
      "channel=stable\n"
      "source=github:henriquefarisco/CapyOS\n"
      "manifest=/system/update/cache/latest.ini\n";
  struct vfs_stat st;

  if (ensure_directory("/system") != 0 ||
      ensure_directory("/system/update") != 0 ||
      ensure_directory("/system/update/cache") != 0) {
    return -1;
  }
  if (vfs_stat_path("/system/update/repository.ini", &st) == 0) {
    return 0;
  }
  return write_text_file("/system/update/repository.ini", repo_defaults);
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
    log_buffer_append(seg);
  }
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
                              const char *keyboard, const char *language,
                              const char *network_mode,
                              const char *service_target, int splash_enabled,
                              uint32_t ipv4_addr,
                              uint32_t ipv4_mask, uint32_t ipv4_gateway,
                              uint32_t ipv4_dns) {
  const char *splash_value = splash_enabled ? "enabled" : "disabled";
  const char *keyboard_value = keyboard ? keyboard : "us";
  const char *language_value = system_language_or_default(language);
  const char *network_mode_value = system_network_mode_or_default(network_mode);
  const char *service_target_value =
      system_service_target_or_default(service_target);
  char ipv4_text[16];
  char mask_text[16];
  char gateway_text[16];
  char dns_text[16];
  struct file *f = vfs_open("/system/config.ini", VFS_OPEN_READ);
  if (!f) {
    vga_write("Falha ao reabrir configuracao em /system/config.ini.\n");
    return -1;
  }
  char buffer[384];
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
  int language_ok = 0;
  int network_mode_ok = 0;
  int service_target_ok = 0;
  int splash_ok = 0;
  int ipv4_ok = 0;
  int mask_ok = 0;
  int gateway_ok = 0;
  int dns_ok = 0;

  system_ipv4_to_string(ipv4_addr, ipv4_text);
  system_ipv4_to_string(ipv4_mask, mask_text);
  system_ipv4_to_string(ipv4_gateway, gateway_text);
  system_ipv4_to_string(ipv4_dns, dns_text);

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
        } else if (!language_ok &&
                   config_line_equals(&buffer[start], len, "language",
                                      language_value)) {
          language_ok = 1;
        } else if (!network_mode_ok &&
                   config_line_equals(&buffer[start], len, "network_mode",
                                      network_mode_value)) {
          network_mode_ok = 1;
        } else if (!service_target_ok &&
                   config_line_equals(&buffer[start], len, "service_target",
                                      service_target_value)) {
          service_target_ok = 1;
        } else if (!splash_ok && config_line_equals(&buffer[start], len,
                                                    "splash", splash_value)) {
          splash_ok = 1;
        } else if (!ipv4_ok &&
                   config_line_equals(&buffer[start], len, "ipv4", ipv4_text)) {
          ipv4_ok = 1;
        } else if (!mask_ok &&
                   config_line_equals(&buffer[start], len, "mask", mask_text)) {
          mask_ok = 1;
        } else if (!gateway_ok &&
                   config_line_equals(&buffer[start], len, "gateway",
                                      gateway_text)) {
          gateway_ok = 1;
        } else if (!dns_ok &&
                   config_line_equals(&buffer[start], len, "dns", dns_text)) {
          dns_ok = 1;
        }
      }
      start = i + 1;
    }
  }

  if (!hostname_ok || !theme_ok || !keyboard_ok || !language_ok ||
      !network_mode_ok || !service_target_ok || !splash_ok || !ipv4_ok ||
      !mask_ok || !gateway_ok || !dns_ok) {
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
  const char *service_target_value =
      system_service_target_or_default(settings->service_target);
  char ipv4_text[16];
  char mask_text[16];
  char gateway_text[16];
  char dns_text[16];
  char config_buffer[512];
  config_buffer[0] = '\0';
  system_ipv4_to_string(settings->ipv4_addr, ipv4_text);
  system_ipv4_to_string(settings->ipv4_mask, mask_text);
  system_ipv4_to_string(settings->ipv4_gateway, gateway_text);
  system_ipv4_to_string(settings->ipv4_dns, dns_text);
  buffer_append(config_buffer, sizeof(config_buffer), "hostname=");
  buffer_append(config_buffer, sizeof(config_buffer), settings->hostname);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "theme=");
  buffer_append(config_buffer, sizeof(config_buffer), settings->theme);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "keyboard=");
  buffer_append(config_buffer, sizeof(config_buffer), settings->keyboard_layout);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "language=");
  buffer_append(config_buffer, sizeof(config_buffer),
                system_language_or_default(settings->language));
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "network_mode=");
  buffer_append(config_buffer, sizeof(config_buffer),
                system_network_mode_or_default(settings->network_mode));
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "service_target=");
  buffer_append(config_buffer, sizeof(config_buffer), service_target_value);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "splash=");
  buffer_append(config_buffer, sizeof(config_buffer), splash_value);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "ipv4=");
  buffer_append(config_buffer, sizeof(config_buffer), ipv4_text);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "mask=");
  buffer_append(config_buffer, sizeof(config_buffer), mask_text);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "gateway=");
  buffer_append(config_buffer, sizeof(config_buffer), gateway_text);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");
  buffer_append(config_buffer, sizeof(config_buffer), "dns=");
  buffer_append(config_buffer, sizeof(config_buffer), dns_text);
  buffer_append(config_buffer, sizeof(config_buffer), "\n");

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

static int wizard_menu_select(const char *title, const char *language,
                              const char *const *items, size_t count,
                              size_t default_index);

static void wizard_draw_setup_header(uint32_t progress, const char *title) {
  char pct[8];

  vga_clear();
  vga_write("CAPYOS SETUP");
  vga_newline();
  vga_newline();

  pct[0] = '\0';
  u32_to_string(progress, pct, sizeof(pct));
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

static size_t wizard_prompt_setup(uint32_t progress, const char *title,
                                  const char *prompt, char *buffer,
                                  size_t buffer_len, int secret) {
  wizard_draw_setup_header(progress, title);
  return wizard_prompt(prompt, buffer, buffer_len, secret);
}

static int wizard_menu_select_setup(uint32_t progress, const char *title,
                                    const char *language,
                                    const char *const *items, size_t count,
                                    size_t default_index) {
  char menu_title[160];
  char pct[8];

  menu_title[0] = '\0';
  u32_to_string(progress, pct, sizeof(pct));
  buffer_append(menu_title, sizeof(menu_title), "[");
  buffer_append(menu_title, sizeof(menu_title), pct);
  buffer_append(menu_title, sizeof(menu_title), "%] ");
  buffer_append(menu_title, sizeof(menu_title), title ? title : "");
  return wizard_menu_select(menu_title, language, items, count, default_index);
}

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

static const char *system_ui_menu_enabled(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Enabled";
  }
  if (strings_equal(normalized, "es")) {
    return "Activado";
  }
  return "Ativado";
}

static const char *system_ui_menu_disabled(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Disabled";
  }
  if (strings_equal(normalized, "es")) {
    return "Desactivado";
  }
  return "Desativado";
}

static const char *system_ui_splash_menu_title(const char *language) {
  const char *normalized = system_language_or_default(language);
  if (strings_equal(normalized, "en")) {
    return "Animated splash";
  }
  if (strings_equal(normalized, "es")) {
    return "Splash animado";
  }
  return "Splash animado";
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
    u32_to_string((uint32_t)(i + 1u), idx, sizeof(idx));
    buffer_append(line, sizeof(line), (i == selected) ? "> " : "  ");
    buffer_append(line, sizeof(line), "[");
    buffer_append(line, sizeof(line), idx);
    buffer_append(line, sizeof(line), "] ");
    buffer_append(line, sizeof(line), items[i] ? items[i] : "");
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

enum system_ui_text_id {
  SYS_UI_PASSWORD_CONFIRM_PROMPT = 0,
  SYS_UI_PASSWORD_EMPTY,
  SYS_UI_PASSWORD_MISMATCH,
  SYS_UI_LAYOUTS_AVAILABLE,
  SYS_UI_LAYOUT_PROMPT,
  SYS_UI_LAYOUT_APPLIED_PREFIX,
  SYS_UI_LAYOUT_UNKNOWN,
  SYS_UI_HOSTNAME_PROMPT,
  SYS_UI_HOSTNAME_DEFINED_PREFIX,
  SYS_UI_THEMES_AVAILABLE,
  SYS_UI_THEME_PROMPT,
  SYS_UI_THEME_SELECTED_PREFIX,
  SYS_UI_SPLASH_PROMPT,
  SYS_UI_SPLASH_ENABLED,
  SYS_UI_SPLASH_DISABLED,
  SYS_UI_ADMIN_USER_PROMPT,
  SYS_UI_ADMIN_USER_INVALID,
  SYS_UI_ADMIN_HOME_CREATE_FAIL,
  SYS_UI_ADMIN_HOME_UNAVAILABLE,
  SYS_UI_ADMIN_HOME_PERM_WARNING,
  SYS_UI_ADMIN_EXISTS,
  SYS_UI_ADMIN_HOME_REBUILD_PREFIX,
  SYS_UI_ADMIN_HOME_REBUILD_FAIL,
  SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX,
  SYS_UI_ADMIN_REGISTER_FAIL,
  SYS_UI_ADMIN_RECORD_BUILD_FAIL,
  SYS_UI_ADMIN_SAVE_FAIL,
  SYS_UI_ADMIN_AUTH_REBUILD_FAIL,
  SYS_UI_ADMIN_USERDB_REBUILD_FAIL,
  SYS_UI_ADMIN_VALIDATED,
  SYS_UI_CONFIG_WRITE_FAIL,
  SYS_UI_CONFIG_VALIDATED,
  SYS_UI_FIRST_BOOT_COMPLETE_FAIL,
  SYS_UI_LOGIN_TITLE,
  SYS_UI_LOGIN_HOST_PREFIX,
  SYS_UI_LOGIN_USERNAME_PROMPT,
  SYS_UI_LOGIN_PASSWORD_PROMPT,
  SYS_UI_LOGIN_CREDENTIALS_REQUIRED,
  SYS_UI_LOGIN_INVALID,
};

static const char *system_ui_text(const char *language,
                                  enum system_ui_text_id id) {
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

static int prompt_password_pair(const char *label, char *password,
                                size_t password_len,
                                const char *language) {
  for (int tries = 0; tries < 3; ++tries) {
    char confirm[TTY_BUFFER_MAX];
    memory_zero(password, password_len);
    memory_zero(confirm, sizeof(confirm));
    size_t plen = wizard_prompt(label, password, password_len, 1);
    size_t clen = wizard_prompt(
        system_ui_text(language, SYS_UI_PASSWORD_CONFIRM_PROMPT), confirm,
        sizeof(confirm), 1);
    if (plen == 0 || clen == 0) {
      print_line(system_ui_text(language, SYS_UI_PASSWORD_EMPTY));
      continue;
    }
    if (plen != clen) {
      print_line(system_ui_text(language, SYS_UI_PASSWORD_MISMATCH));
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
      print_line(system_ui_text(language, SYS_UI_PASSWORD_MISMATCH));
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

static void dbg_print(const char *a, const char *b) {
  if (!g_setup_debug)
    return;
  if (a)
    log_buffer_append(a);
  if (b)
    log_buffer_append(b);
  log_buffer_append("\n");
  log_flush_pending();
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
    log_buffer_append(prefix);
  }
  if (path) {
    log_buffer_append(path);
  }
  log_buffer_append(" [heap ");
  log_buffer_append(used);
  log_buffer_append("/");
  log_buffer_append(total);
  log_buffer_append("]\n");
  log_flush_pending();
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

static int validate_admin_username(const char *username) {
  if (!username || !username[0]) {
    return 0;
  }
  size_t len = cstring_length(username);
  if (len == 0 || len >= USER_NAME_MAX) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!valid_username_char(username[i])) {
      return 0;
    }
  }
  return 1;
}

static const char *validate_theme(const char *input) {
  if (!input || cstring_length(input) == 0) {
    return "capyos";
  }
  if (strings_equal(input, "capyos") || strings_equal(input, "CAPYOS") ||
      strings_equal(input, "ocean") ||
      strings_equal(input, "forest")) {
    return strings_equal(input, "CAPYOS") ? "capyos" : input;
  }
  return "capyos";
}

void __attribute__((weak)) system_platform_apply_theme(const char *theme) {
  (void)theme;
}

void __attribute__((weak))
system_platform_sync_theme(const struct system_settings *settings) {
  (void)settings;
}

int system_mark_first_boot_complete(void) {
  if (ensure_directory("/system") != 0) {
    return -1;
  }
  return write_text_file("/system/first-run.done", "completed\n");
}

/* Implementacao da configuracao inicial. */
static int first_boot_setup_impl(void) {
  const char *setup_language = system_language_or_default(g_boot_default_language);
  if (strings_equal(setup_language, "en")) {
    print_line("=== CapyOS Initial Setup ===");
    print_line("This wizard prepares users, settings, and the base system structure.");
  } else if (strings_equal(setup_language, "es")) {
    print_line("=== Configuracion Inicial de CapyOS ===");
    print_line("Este asistente prepara usuarios, configuracion y la estructura basica.");
  } else {
    print_line("=== Assistente CapyOS - Configuracao Inicial ===");
    print_line(
        "Este assistente prepara usuarios, configuracao e estrutura basica.");
  }
  vga_newline();

  g_setup_debug = 0;

  const char *proc_dirs = "preparacao de diretorios padrao";
  log_process_begin(proc_dirs);
  log_process_begin_success(proc_dirs);
  dbg_print_heap("[setup] ensure path: ", "/bin");
  if (ensure_directory("/bin") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/etc");
  if (ensure_directory("/etc") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/home");
  if (ensure_directory("/home") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/tmp");
  if (ensure_directory("/tmp") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/var");
  if (ensure_directory("/var") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/var/log");
  if (ensure_directory("/var/log") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/system");
  if (ensure_directory("/system") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  dbg_print_heap("[setup] ensure path: ", "/docs");
  if (ensure_directory("/docs") != 0) {
    print_line("Falha ao preparar estrutura de diretorios.");
    return -1;
  }
  log_process_progress(proc_dirs);
  if (verify_directory_exists("/bin") != 0 ||
      verify_directory_exists("/etc") != 0 ||
      verify_directory_exists("/home") != 0 ||
      verify_directory_exists("/tmp") != 0 ||
      verify_directory_exists("/var") != 0 ||
      verify_directory_exists("/var/log") != 0 ||
      verify_directory_exists("/system") != 0 ||
      verify_directory_exists("/docs") != 0) {
    print_line("Verificacao de diretorios falhou.");
    return -1;
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
  if (write_text_file("/docs/capyos-cli-reference.txt", g_cli_reference_text) !=
      0) {
    print_line("   Aviso: nao foi possivel gravar referencia do CapyCLI.");
  } else {
    print_line(
        "   Referencia CapyCLI pronta em /docs/capyos-cli-reference.txt.");
  }
  if (system_prepare_update_catalog() != 0) {
    print_line("   Aviso: nao foi possivel preparar /system/update.");
  }
  sync_root_device();
  log_process_conclude(proc_docs);
  log_process_finalize(proc_docs);
  log_process_finalize_success(proc_docs);

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
      buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                    keyboard_layout_name(i));
      buffer_append(layout_labels[i], sizeof(layout_labels[i]), " - ");
      buffer_append(layout_labels[i], sizeof(layout_labels[i]),
                    keyboard_layout_description(i));
      layout_items[i] = layout_labels[i];
      if (strings_equal(keyboard_layout_name(i), g_boot_default_keyboard_layout)) {
        selected_layout = i;
      }
    }

    selected_layout = (size_t)wizard_menu_select_setup(
        20u,
        system_ui_text(setup_language, SYS_UI_LAYOUTS_AVAILABLE),
        setup_language, layout_items, layout_count, selected_layout);
    cstring_copy(layout_choice, sizeof(layout_choice),
                 keyboard_layout_name(selected_layout));
    if (keyboard_set_layout_by_name(layout_choice) != 0) {
      cstring_copy(layout_choice, sizeof(layout_choice), "us");
      keyboard_set_layout_by_name(layout_choice);
      print_line(system_ui_text(setup_language, SYS_UI_LAYOUT_UNKNOWN));
    }
  }

  char hostname[TTY_BUFFER_MAX];
  const char *theme = "capyos";
  int splash_enabled = 1;
  memory_zero(hostname, sizeof(hostname));

  const char *proc_settings = "coleta de configuracoes basicas";
  log_dependency_wait(proc_docs, proc_settings);
  log_process_begin(proc_settings);
  log_process_begin_success(proc_settings);
  size_t hlen =
      wizard_prompt_setup(40u, "Hostname",
                          system_ui_text(setup_language, SYS_UI_HOSTNAME_PROMPT),
                          hostname, sizeof(hostname), 0);
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
        55u,
        system_ui_text(setup_language, SYS_UI_THEMES_AVAILABLE),
        setup_language, theme_items,
        sizeof(theme_items) / sizeof(theme_items[0]), 0);
    theme = validate_theme(theme_pick == 0 ? "capyos"
                           : (theme_pick == 1 ? "ocean" : "forest"));
  }

  {
    const char *splash_items[] = {system_ui_menu_enabled(setup_language),
                                  system_ui_menu_disabled(setup_language)};
    int splash_pick = wizard_menu_select_setup(
        70u, system_ui_splash_menu_title(setup_language), setup_language,
        splash_items, 2, 0);
    splash_enabled = (splash_pick == 0) ? 1 : 0;
  }

  char admin_username[USER_NAME_MAX];
  memory_zero(admin_username, sizeof(admin_username));
  size_t ulen =
      wizard_prompt_setup(85u, "Administrator",
                          system_ui_text(setup_language, SYS_UI_ADMIN_USER_PROMPT),
                          admin_username, sizeof(admin_username), 0);
  if (ulen == 0) {
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }
  if (!validate_admin_username(admin_username)) {
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_USER_INVALID));
    cstring_copy(admin_username, sizeof(admin_username), "admin");
  }

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
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_HOME_CREATE_FAIL));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  if (verify_directory_exists(admin_home) != 0) {
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_HOME_UNAVAILABLE));
    memory_zero(admin_password, sizeof(admin_password));
    return -1;
  }
  struct vfs_metadata home_meta = {admin_uid, admin_gid, 0700};
  if (vfs_set_metadata(admin_home, &home_meta) != 0) {
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_HOME_PERM_WARNING));
  }
  sync_root_device();

  int admin_ready = 0;
  struct user_record existing;
  if (userdb_find(admin_username, &existing) == 0) {
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_EXISTS));
    if (verify_directory_exists(existing.home) != 0) {
      char rebuild_msg[128];
      rebuild_msg[0] = '\0';
      buffer_append(rebuild_msg, sizeof(rebuild_msg),
                    system_ui_text(setup_language,
                                   SYS_UI_ADMIN_HOME_REBUILD_PREFIX));
      buffer_append(rebuild_msg, sizeof(rebuild_msg), existing.home);
      buffer_append(rebuild_msg, sizeof(rebuild_msg), ".");
      print_line(rebuild_msg);
      if (ensure_directory(existing.home) != 0 ||
          verify_directory_exists(existing.home) != 0) {
        print_line(
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
  buffer_append(password_prompt, sizeof(password_prompt),
                system_ui_text(setup_language,
                               SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX));
  buffer_append(password_prompt, sizeof(password_prompt), admin_username);
  buffer_append(password_prompt, sizeof(password_prompt), ": ");

  while (!admin_ready) {
    wizard_draw_setup_header(95u, "Administrator password");
    if (prompt_password_pair(password_prompt, admin_password,
                             sizeof(admin_password), setup_language) != 0) {
      print_line(system_ui_text(setup_language, SYS_UI_ADMIN_REGISTER_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    struct user_record admin;
    if (user_record_init(admin_username, admin_password, "admin", admin_uid,
                         admin_gid, admin_home, &admin) != 0) {
      print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_RECORD_BUILD_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    if (userdb_add(&admin) != 0) {
      print_line(system_ui_text(setup_language, SYS_UI_ADMIN_SAVE_FAIL));
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    sync_root_device();

    struct user_record verify_rec;
    if (userdb_authenticate(admin_username, admin_password, &verify_rec) != 0) {
      print_line(
          system_ui_text(setup_language, SYS_UI_ADMIN_AUTH_REBUILD_FAIL));
      vfs_unlink(USER_DB_PATH);
      if (userdb_ensure() != 0) {
        print_line(
            system_ui_text(setup_language, SYS_UI_ADMIN_USERDB_REBUILD_FAIL));
        memory_zero(admin_password, sizeof(admin_password));
        return -1;
      }
      sync_root_device();
      memory_zero(admin_password, sizeof(admin_password));
      continue;
    }

    (void)user_prefs_save_language(&verify_rec, setup_language);
    admin_uid = admin.uid;
    admin_gid = admin.gid;
    print_line(system_ui_text(setup_language, SYS_UI_ADMIN_VALIDATED));
    memory_zero(admin_password, sizeof(admin_password));
    admin_ready = 1;
  }

  memory_zero(admin_password, sizeof(admin_password));
  log_process_conclude(proc_admin);
  log_process_finalize(proc_admin);
  log_process_finalize_success(proc_admin);

  log_process_progress(proc_settings);
  struct system_settings settings;
  system_settings_set_defaults(&settings);
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
  log_dependency_wait(proc_admin, proc_config);
  log_process_begin(proc_config);
  log_process_begin_success(proc_config);
  if (write_settings_file(&settings) != 0) {
    print_line(system_ui_text(setup_language, SYS_UI_CONFIG_WRITE_FAIL));
    return -1;
  }
  sync_root_device();
  if (verify_config_file(settings.hostname, settings.theme,
                         settings.keyboard_layout, settings.language,
                         settings.network_mode, settings.service_target,
                         settings.splash_enabled, settings.ipv4_addr,
                         settings.ipv4_mask, settings.ipv4_gateway,
                         settings.ipv4_dns) !=
      0) {
    return -1;
  }
  print_line(system_ui_text(setup_language, SYS_UI_CONFIG_VALIDATED));

  if (system_mark_first_boot_complete() != 0) {
    print_line(
        system_ui_text(setup_language, SYS_UI_FIRST_BOOT_COMPLETE_FAIL));
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
    print_line("   Validacao final do registro do administrador concluida.");
    log_user_record_state(&final_rec);
  } else {
    print_line("   Aviso: nao foi possivel reler registro do administrador "
               "apos configuracao.");
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
    cstring_copy(settings->theme, sizeof(settings->theme),
                 strings_equal(value, "CAPYOS") ? "capyos" : value);
  } else if (strings_equal(key, "keyboard")) {
    cstring_copy(settings->keyboard_layout, sizeof(settings->keyboard_layout),
                 value);
  } else if (strings_equal(key, "language")) {
    cstring_copy(settings->language, sizeof(settings->language),
                 system_language_or_default(value));
  } else if (strings_equal(key, "network_mode")) {
    cstring_copy(settings->network_mode, sizeof(settings->network_mode),
                 system_network_mode_or_default(value));
  } else if (strings_equal(key, "service_target")) {
    cstring_copy(settings->service_target, sizeof(settings->service_target),
                 system_service_target_or_default(value));
  } else if (strings_equal(key, "splash")) {
    if (value[0] == 'd' || value[0] == 'D') {
      settings->splash_enabled = 0;
    } else {
      settings->splash_enabled = 1;
    }
  } else if (strings_equal(key, "ipv4")) {
    uint32_t parsed = 0;
    if (system_parse_ipv4(value, &parsed) == 0) {
      settings->ipv4_addr = parsed;
    }
  } else if (strings_equal(key, "mask")) {
    uint32_t parsed = 0;
    if (system_parse_ipv4(value, &parsed) == 0) {
      settings->ipv4_mask = parsed;
    }
  } else if (strings_equal(key, "gateway")) {
    uint32_t parsed = 0;
    if (system_parse_ipv4(value, &parsed) == 0) {
      settings->ipv4_gateway = parsed;
    }
  } else if (strings_equal(key, "dns")) {
    uint32_t parsed = 0;
    if (system_parse_ipv4(value, &parsed) == 0) {
      settings->ipv4_dns = parsed;
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
  struct session_context *previous_session = NULL;
  const struct user_record *previous_user = NULL;
  int rc = 0;
  if (!settings) {
    return -1;
  }

  previous_session = session_active();
  previous_user = previous_session ? session_user(previous_session) : NULL;
  if (previous_user && previous_user->username[0]) {
    struct vfs_metadata meta;
    meta.uid = previous_user->uid;
    meta.gid = previous_user->gid;
    meta.perm = 0644;
    (void)vfs_set_metadata("/system/config.ini", &meta);
  }
  session_set_active(NULL);

  if (write_settings_file(settings) != 0) {
    rc = -1;
    goto done;
  }
  sync_root_device();
  if (verify_config_file(settings->hostname, settings->theme,
                         settings->keyboard_layout, settings->language,
                         settings->network_mode, settings->service_target,
                         settings->splash_enabled, settings->ipv4_addr,
                         settings->ipv4_mask, settings->ipv4_gateway,
                         settings->ipv4_dns) != 0) {
    rc = -1;
    goto done;
  }

done:
  session_set_active(previous_session);
  return rc;
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

int system_save_theme(const char *theme) {
  struct system_settings settings;
  const char *validated = validate_theme(theme);
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  cstring_copy(settings.theme, sizeof(settings.theme), validated);
  return system_save_settings(&settings);
}

int system_save_splash_enabled(int enabled) {
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  settings.splash_enabled = enabled ? 1 : 0;
  return system_save_settings(&settings);
}

int system_save_network_ipv4(uint32_t addr, uint32_t mask, uint32_t gateway,
                             uint32_t dns) {
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  cstring_copy(settings.network_mode, sizeof(settings.network_mode), "static");
  settings.ipv4_addr = addr;
  settings.ipv4_mask = mask;
  settings.ipv4_gateway = gateway;
  settings.ipv4_dns = dns;
  return system_save_settings(&settings);
}

int system_save_network_mode(const char *mode) {
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  cstring_copy(settings.network_mode, sizeof(settings.network_mode),
               system_network_mode_or_default(mode));
  return system_save_settings(&settings);
}

int system_save_service_target(const char *target) {
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  cstring_copy(settings.service_target, sizeof(settings.service_target),
               system_service_target_or_default(target));
  return system_save_settings(&settings);
}

void system_apply_keyboard_layout(const struct system_settings *settings) {
  const char *layout = (settings && settings->keyboard_layout[0])
                           ? settings->keyboard_layout
                           : "us";
  if (keyboard_set_layout_by_name(layout) != 0) {
    log_event("[kbd] layout desconhecido em config.ini; revertendo para 'us'.");
    keyboard_set_layout_by_name("us");
  }
}

int system_login(struct session_context *session,
                 const struct system_settings *settings) {
  const char *language =
      settings ? system_language_or_default(settings->language) : "en";
  if (!session) {
    return -1;
  }
  const char *proc_login = "autenticacao de usuario";
  log_process_begin(proc_login);
  log_process_begin_success(proc_login);
  vga_newline();
  print_line(system_ui_text(language, SYS_UI_LOGIN_TITLE));
  if (settings) {
    char host_msg[128];
    host_msg[0] = '\0';
    buffer_append(host_msg, sizeof(host_msg),
                  system_ui_text(language, SYS_UI_LOGIN_HOST_PREFIX));
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
    size_t ulen = wizard_prompt(
        system_ui_text(language, SYS_UI_LOGIN_USERNAME_PROMPT), username,
        sizeof(username), 0);
    size_t plen = wizard_prompt(
        system_ui_text(language, SYS_UI_LOGIN_PASSWORD_PROMPT), password,
        sizeof(password), 1);
    if (ulen == 0 || plen == 0) {
      print_line(system_ui_text(language, SYS_UI_LOGIN_CREDENTIALS_REQUIRED));
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
      session_begin(session, &record, language);
      char welcome[160];
      language = session_language(session);
      welcome[0] = '\0';
      buffer_append(welcome, sizeof(welcome),
                    localization_text_for(language, LOC_TEXT_WELCOME_PREFIX));
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
    print_line(system_ui_text(language, SYS_UI_LOGIN_INVALID));
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
    system_platform_apply_theme("capyos");
    system_platform_sync_theme(settings);
    return;
  }
  if (strings_equal(theme, "ocean")) {
    vga_set_color(11, 1); // cyan on blue
  } else if (strings_equal(theme, "forest")) {
    vga_set_color(10, 2); // green on greenish
  } else {
    vga_set_color(7, 0); // default capyos
    theme = "capyos";
  }
  system_platform_apply_theme(theme);
  system_platform_sync_theme(settings);
}

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
