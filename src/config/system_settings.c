/* system_settings.c: settings load/save, config file I/O, update catalog. */
#include "internal/config_internal.h"
#include "core/version.h"

/* ---- validate_theme (forward) ---- */
static const char *validate_theme(const char *input) {
  if (!input || cstring_length(input) == 0) {
    return "capyos";
  }
  /* "love"/"high-contrast" adicionados em 2026-05-01 (post-M5 W4).
   * "love" e o nome canonico; "rosa"/"pink" sao apelidos legados que
   * existiam em builds anteriores e continuam aceitos por arquivos
   * /system/config.ini que migraram de versao, mas sempre normalizam
   * para "love". A UI nao expoe mais os nomes antigos. */
  if (strings_equal(input, "love") || strings_equal(input, "rosa") ||
      strings_equal(input, "pink")) {
    return "love";
  }
  if (strings_equal(input, "capyos") || strings_equal(input, "CAPYOS") ||
      strings_equal(input, "ocean") || strings_equal(input, "forest") ||
      strings_equal(input, "high-contrast")) {
    return strings_equal(input, "CAPYOS") ? "capyos" : input;
  }
  return "capyos";
}

/* ---- settings defaults ---- */
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
  cstring_copy(settings->update_channel, sizeof(settings->update_channel),
               system_update_channel_or_default(NULL));
  cstring_copy(settings->network_mode, sizeof(settings->network_mode),
               system_network_mode_or_default(NULL));
  cstring_copy(settings->service_target, sizeof(settings->service_target),
               system_service_target_or_default(NULL));
  settings->ipv4_addr = 0;
  settings->ipv4_mask = 0;
  settings->ipv4_gateway = 0;
  settings->ipv4_dns = 0;
  settings->splash_enabled = 1;
  settings->diagnostics_enabled = 0;
  /* Etapa F4 homepage (2026-05-03): default = wikipedia.org.
   * O campo `browser_homepage` permanece em config.ini apos a
   * erradicacao do capybrowser na sessao 6 (2026-05-05) -- sera
   * reusado pelo port do Firefox quando aterrissar.
   * Ver docs/plans/active/firefox-port-roadmap.md. */
  cstring_copy(settings->browser_homepage, sizeof(settings->browser_homepage),
               "https://wikipedia.org");
}

/* ---- config line parser ---- */
static int config_line_equals(const char *line, size_t len, const char *key,
                              const char *value) {
  size_t klen = cstring_length(key);
  size_t vlen = value ? cstring_length(value) : 0;
  if (len < klen + 1) {
    return 0;
  }
  for (size_t i = 0; i < klen; ++i) {
    if (line[i] != key[i]) {
      return 0;
    }
  }
  if (line[klen] != '=') {
    return 0;
  }
  size_t val_start = klen + 1;
  size_t val_len = len - val_start;
  if (val_len != vlen) {
    return 0;
  }
  for (size_t i = 0; i < vlen; ++i) {
    if (line[val_start + i] != value[i]) {
      return 0;
    }
  }
  return 1;
}

/* ---- update file helpers ---- */
static int system_update_ensure_file(const char *path) {
  struct dentry *d = NULL;
  struct vfs_metadata meta;
  if (!path) {
    return -1;
  }
  if (vfs_lookup(path, &d) == 0 && d) {
    if (d->refcount) {
      d->refcount--;
    }
    return 0;
  }
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
    return -1;
  }
  meta.uid = 0;
  meta.gid = 0;
  meta.perm = 0644;
  vfs_set_metadata(path, &meta);
  return 0;
}

static int system_update_version_core_ref(const char *version, char *out,
                                          size_t out_size) {
  const char *p = version;
  size_t dots = 0u;
  size_t written = 0u;

  if (!out || out_size == 0u) {
    return -1;
  }
  out[0] = '\0';
  if (!p || !p[0]) {
    return -1;
  }
  if (*p == 'v' || *p == 'V') {
    ++p;
  }
  config_buffer_append(out, out_size, "refs/tags/v");
  written = cstring_length(out);
  while (*p && *p != '-' && *p != '+' && written + 1u < out_size) {
    char next[2];
    if (!((*p >= '0' && *p <= '9') || *p == '.')) {
      break;
    }
    if (*p == '.') {
      ++dots;
    }
    next[0] = *p++;
    next[1] = '\0';
    config_buffer_append(out, out_size, next);
    written = cstring_length(out);
  }
  return dots == 2u ? 0 : -1;
}

static void system_update_remote_manifest_url(const char *channel, char *out,
                                              size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  config_buffer_append(out, out_size,
                       "https://raw.githubusercontent.com/henriquefarisco/"
                       "CapyOS/");
  if (strings_equal(system_update_channel_or_default(channel), "develop")) {
    config_buffer_append(out, out_size, "refs/heads/");
    config_buffer_append(out, out_size,
                         system_update_branch_for_channel(channel));
  } else {
    char stable_ref[32];
    if (system_update_version_core_ref(CAPYOS_VERSION_EXTENDED, stable_ref,
                                       sizeof(stable_ref)) != 0) {
      cstring_copy(stable_ref, sizeof(stable_ref), "refs/heads/main");
    }
    config_buffer_append(out, out_size, stable_ref);
  }
  config_buffer_append(out, out_size, "/system/update/latest.ini");
}

/* ---- update catalog ---- */
int system_prepare_update_catalog(void) {
  char repo_defaults[512];
  char remote_manifest[192];
  struct vfs_stat st;
  const char *channel = system_update_channel_or_default(NULL);

  if (config_ensure_directory("/system") != 0 ||
      config_ensure_directory("/system/update") != 0 ||
      config_ensure_directory("/system/update/cache") != 0 ||
      config_ensure_directory("/system/update/staged") != 0) {
    return -1;
  }
  if (vfs_stat_path("/system/update/repository.ini", &st) == 0) {
    return 0;
  }
  system_update_remote_manifest_url(channel, remote_manifest,
                                    sizeof(remote_manifest));
  repo_defaults[0] = '\0';
  config_buffer_append(repo_defaults, sizeof(repo_defaults), "channel=");
  config_buffer_append(repo_defaults, sizeof(repo_defaults), channel);
  config_buffer_append(repo_defaults, sizeof(repo_defaults), "\nbranch=");
  config_buffer_append(repo_defaults, sizeof(repo_defaults),
                       system_update_branch_for_channel(channel));
  config_buffer_append(repo_defaults, sizeof(repo_defaults), "\nsource=");
  config_buffer_append(repo_defaults, sizeof(repo_defaults),
                       "github:henriquefarisco/CapyOS");
  config_buffer_append(repo_defaults, sizeof(repo_defaults), "\nmanifest=");
  config_buffer_append(repo_defaults, sizeof(repo_defaults),
                       system_update_manifest_for_channel(channel));
  config_buffer_append(repo_defaults, sizeof(repo_defaults),
                       "\nremote_manifest=");
  config_buffer_append(repo_defaults, sizeof(repo_defaults), remote_manifest);
  config_buffer_append(repo_defaults, sizeof(repo_defaults), "\n");
  if (config_write_text_file("/system/update/repository.ini", repo_defaults) !=
      0) {
    return -1;
  }
  if (system_update_ensure_file("/system/update/latest.ini") != 0 ||
      system_update_ensure_file("/system/update/staged.ini") != 0 ||
      system_update_ensure_file("/system/update/state.ini") != 0) {
    return -1;
  }
  return 0;
}

/* ---- verify config file ---- */
int config_verify_config_file(const char *hostname, const char *theme,
                              const char *keyboard, const char *language,
                              const char *update_channel,
                              const char *network_mode,
                              const char *service_target, int splash_enabled,
                              uint32_t ipv4_addr, uint32_t ipv4_mask,
                              uint32_t ipv4_gateway, uint32_t ipv4_dns) {
  const char *splash_value = splash_enabled ? "enabled" : "disabled";
  const char *keyboard_value = keyboard ? keyboard : "us";
  const char *language_value = system_language_or_default(language);
  const char *update_channel_value =
      system_update_channel_or_default(update_channel);
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
  /* Etapa F4 homepage (2026-05-03): bump de 384 -> 1024 para que
   * a linha `browser_homepage=...` (ate ~280 bytes) nao corte o
   * tail do arquivo durante o re-read pos-save. Verifier ignora
   * a linha do homepage (verifica apenas os campos legados), mas
   * precisa que o read inteiro caiba para nao perder os campos
   * que podem aparecer DEPOIS no arquivo no futuro. */
  char buffer[1024];
  long read = vfs_read(f, buffer, sizeof(buffer) - 1);
  vfs_close(f);
  if (read <= 0) {
    vga_write("Arquivo de configuracao vazio ou inacessivel.\n");
    return -1;
  }
  buffer[read] = '\0';

  int hostname_ok = 0, theme_ok = 0, keyboard_ok = 0, language_ok = 0;
  int update_channel_ok = 0, network_mode_ok = 0, service_target_ok = 0;
  int splash_ok = 0, ipv4_ok = 0, mask_ok = 0, gateway_ok = 0, dns_ok = 0;

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
        } else if (!update_channel_ok &&
                   config_line_equals(&buffer[start], len, "update_channel",
                                      update_channel_value)) {
          update_channel_ok = 1;
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
      !update_channel_ok || !network_mode_ok || !service_target_ok ||
      !splash_ok || !ipv4_ok || !mask_ok || !gateway_ok || !dns_ok) {
    vga_write("Falha ao validar conteudo de /system/config.ini.\n");
    return -1;
  }
  return 0;
}

/* ---- write settings file ---- */
int config_write_settings_file(const struct system_settings *settings) {
  if (!settings) {
    return -1;
  }
  if (config_ensure_directory("/system") != 0) {
    return -1;
  }

  const char *splash_value = settings->splash_enabled ? "enabled" : "disabled";
  const char *service_target_value =
      system_service_target_or_default(settings->service_target);
  char ipv4_text[16], mask_text[16], gateway_text[16], dns_text[16];
  /* Etapa F4 homepage (2026-05-03): bump de 512 -> 1024 para
   * acomodar `browser_homepage=` + URL ate 255 chars sem perder
   * truncation safety dos demais campos. */
  char config_buffer[1024];
  config_buffer[0] = '\0';
  system_ipv4_to_string(settings->ipv4_addr, ipv4_text);
  system_ipv4_to_string(settings->ipv4_mask, mask_text);
  system_ipv4_to_string(settings->ipv4_gateway, gateway_text);
  system_ipv4_to_string(settings->ipv4_dns, dns_text);
  config_buffer_append(config_buffer, sizeof(config_buffer), "hostname=");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       settings->hostname);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "theme=");
  config_buffer_append(config_buffer, sizeof(config_buffer), settings->theme);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "keyboard=");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       settings->keyboard_layout);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "language=");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       system_language_or_default(settings->language));
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       "update_channel=");
  config_buffer_append(
      config_buffer, sizeof(config_buffer),
      system_update_channel_or_default(settings->update_channel));
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "network_mode=");
  config_buffer_append(
      config_buffer, sizeof(config_buffer),
      system_network_mode_or_default(settings->network_mode));
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       "service_target=");
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       service_target_value);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "splash=");
  config_buffer_append(config_buffer, sizeof(config_buffer), splash_value);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "ipv4=");
  config_buffer_append(config_buffer, sizeof(config_buffer), ipv4_text);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "mask=");
  config_buffer_append(config_buffer, sizeof(config_buffer), mask_text);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "gateway=");
  config_buffer_append(config_buffer, sizeof(config_buffer), gateway_text);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  config_buffer_append(config_buffer, sizeof(config_buffer), "dns=");
  config_buffer_append(config_buffer, sizeof(config_buffer), dns_text);
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");
  /* Etapa F4 homepage (2026-05-03): persiste a homepage do browser.
   * Se vazia (que so deveria acontecer se um config antigo for
   * carregado e nao tocou no campo), grava o default explicito para
   * que o config.ini pos-save sempre tenha um valor concreto. */
  config_buffer_append(config_buffer, sizeof(config_buffer),
                       "browser_homepage=");
  config_buffer_append(
      config_buffer, sizeof(config_buffer),
      settings->browser_homepage[0] ? settings->browser_homepage
                                    : "https://wikipedia.org");
  config_buffer_append(config_buffer, sizeof(config_buffer), "\n");

  return config_write_text_file("/system/config.ini", config_buffer);
}

/* ---- write update repository file ---- */
int config_write_update_repository_file(
    const struct system_settings *settings) {
  char repo_buffer[512];
  char remote_manifest[192];
  const char *channel =
      settings ? system_update_channel_or_default(settings->update_channel)
               : system_update_channel_or_default(NULL);

  if (config_ensure_directory("/system") != 0 ||
      config_ensure_directory("/system/update") != 0 ||
      config_ensure_directory("/system/update/cache") != 0 ||
      config_ensure_directory("/system/update/staged") != 0) {
    return -1;
  }

  system_update_remote_manifest_url(channel, remote_manifest,
                                    sizeof(remote_manifest));
  repo_buffer[0] = '\0';
  config_buffer_append(repo_buffer, sizeof(repo_buffer), "channel=");
  config_buffer_append(repo_buffer, sizeof(repo_buffer), channel);
  config_buffer_append(repo_buffer, sizeof(repo_buffer), "\nbranch=");
  config_buffer_append(repo_buffer, sizeof(repo_buffer),
                       system_update_branch_for_channel(channel));
  config_buffer_append(repo_buffer, sizeof(repo_buffer), "\nsource=");
  config_buffer_append(repo_buffer, sizeof(repo_buffer),
                       "github:henriquefarisco/CapyOS");
  config_buffer_append(repo_buffer, sizeof(repo_buffer), "\nmanifest=");
  config_buffer_append(repo_buffer, sizeof(repo_buffer),
                       system_update_manifest_for_channel(channel));
  config_buffer_append(repo_buffer, sizeof(repo_buffer),
                       "\nremote_manifest=");
  config_buffer_append(repo_buffer, sizeof(repo_buffer), remote_manifest);
  config_buffer_append(repo_buffer, sizeof(repo_buffer), "\n");
  return config_write_text_file("/system/update/repository.ini", repo_buffer);
}

/* ---- apply config line ---- */
static void apply_config_line(struct system_settings *settings,
                              const char *line, size_t len) {
  size_t eq = 0;
  while (eq < len && line[eq] != '=') {
    ++eq;
  }
  if (eq == 0 || eq >= len) {
    return;
  }
  /* Etapa F4 homepage (2026-05-03): bump dos buffers porque
   * `browser_homepage` (16 chars) nao cabe em key[16] (klen <= 15)
   * e a URL pode chegar a 255 chars. Antes era key[16]/value[64]. */
  char key[32];
  char value[SYSTEM_BROWSER_HOMEPAGE_MAX];
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
  } else if (strings_equal(key, "update_channel")) {
    cstring_copy(settings->update_channel, sizeof(settings->update_channel),
                 system_update_channel_or_default(value));
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
  } else if (strings_equal(key, "browser_homepage")) {
    /* URL inicial do browser. Validacao de esquema/comprimento
     * acontecera no Firefox quando portado (sessao 6 erradicou o
     * capybrowser; o campo persiste para o sucessor). */
    if (value[0] != '\0') {
      cstring_copy(settings->browser_homepage,
                   sizeof(settings->browser_homepage), value);
    }
  }
}

/* ---- load settings ---- */
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

/* ---- save settings ---- */
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

  if (config_write_settings_file(settings) != 0) {
    rc = -1;
    goto done;
  }
  config_sync_root_device();
  if (config_verify_config_file(
          settings->hostname, settings->theme, settings->keyboard_layout,
          settings->language, settings->update_channel, settings->network_mode,
          settings->service_target, settings->splash_enabled,
          settings->ipv4_addr, settings->ipv4_mask, settings->ipv4_gateway,
          settings->ipv4_dns) != 0) {
    rc = -1;
    goto done;
  }
  if (config_write_update_repository_file(settings) != 0) {
    rc = -1;
    goto done;
  }

done:
  session_set_active(previous_session);
  return rc;
}

/* ---- convenience save functions ---- */
int system_save_keyboard_layout(const char *layout) {
  if (!layout) {
    return -1;
  }
  struct system_settings settings;
  system_settings_set_defaults(&settings);
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

int system_save_update_channel(const char *channel) {
  struct system_settings settings;
  system_settings_set_defaults(&settings);
  system_load_settings(&settings);
  cstring_copy(settings.update_channel, sizeof(settings.update_channel),
               system_update_channel_or_default(channel));
  return system_save_settings(&settings);
}
