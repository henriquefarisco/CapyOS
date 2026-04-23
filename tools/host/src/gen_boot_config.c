#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot/boot_config.h"

static void set_error(char *err, size_t err_len, const char *msg) {
  if (!err || err_len == 0) return;
  while (err_len > 1 && *msg) {
    *err++ = *msg++;
    err_len--;
  }
  *err = '\0';
}

static int has_text(const char *value) {
  return value && value[0] != '\0';
}

static int text_equal_ci(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
    i++;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static int copy_field(char *dst, size_t dst_len, const char *src,
                      const char *field_name, char *err, size_t err_len) {
  size_t len = 0;
  if (!dst || dst_len == 0) return -1;
  dst[0] = '\0';
  if (!src) return 0;
  len = strlen(src);
  if (len >= dst_len) {
    set_error(err, err_len, field_name);
    return -1;
  }
  memcpy(dst, src, len + 1);
  return 0;
}

static int normalize_volume_key(const char *raw, char *dst, size_t dst_len,
                                char *err, size_t err_len) {
  size_t out = 0;
  if (!dst || dst_len == 0) return -1;
  dst[0] = '\0';
  if (!has_text(raw)) return 0;

  for (size_t i = 0; raw[i]; i++) {
    char ch = raw[i];
    if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      continue;
    }
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
      set_error(err, err_len, "volume key must contain only letters and numbers");
      return -1;
    }
    if (out + 1 >= dst_len) {
      set_error(err, err_len, "volume key too long");
      return -1;
    }
    dst[out++] = ch;
  }

  if (out < 8) {
    set_error(err, err_len, "volume key too short");
    return -1;
  }

  dst[out] = '\0';
  return 0;
}

static int parse_splash(const char *value, uint8_t default_value, uint8_t *out,
                        char *err, size_t err_len) {
  if (!out) return -1;
  if (!has_text(value)) {
    *out = default_value;
    return 0;
  }
  if (strcmp(value, "1") == 0 || text_equal_ci(value, "true") ||
      text_equal_ci(value, "yes") || text_equal_ci(value, "enabled")) {
    *out = 1;
    return 0;
  }
  if (strcmp(value, "0") == 0 || text_equal_ci(value, "false") ||
      text_equal_ci(value, "no") || text_equal_ci(value, "disabled")) {
    *out = 0;
    return 0;
  }
  set_error(err, err_len, "invalid splash value");
  return -1;
}

int gen_boot_config_build(struct boot_config_sector *cfg, const char *layout,
                          const char *language, const char *volume_key,
                          const char *hostname, const char *theme,
                          const char *admin_user, const char *admin_pass,
                          const char *splash, char *err, size_t err_len) {
  char normalized_key[sizeof(cfg->volume_key)];
  int has_setup = 0;
  uint8_t splash_enabled = 0;

  if (!cfg) {
    set_error(err, err_len, "missing boot config output");
    return -1;
  }

  if (!has_text(layout)) layout = "us";
  if (!has_text(language)) language = "en";

  memset(cfg, 0, sizeof(*cfg));
  cfg->magic = BOOT_CONFIG_MAGIC;
  cfg->version = BOOT_CONFIG_VERSION;

  if (copy_field(cfg->keyboard_layout, sizeof(cfg->keyboard_layout), layout,
                 "keyboard layout too long", err, err_len) != 0) {
    return -1;
  }
  if (copy_field(cfg->language, sizeof(cfg->language), language,
                 "language too long", err, err_len) != 0) {
    return -1;
  }
  if (normalize_volume_key(volume_key, normalized_key, sizeof(normalized_key),
                           err, err_len) != 0) {
    return -1;
  }
  if (has_text(normalized_key)) {
    if (copy_field(cfg->volume_key, sizeof(cfg->volume_key), normalized_key,
                   "volume key too long", err, err_len) != 0) {
      return -1;
    }
    cfg->flags |= BOOT_CONFIG_FLAG_HAS_VOLUME_KEY;
  }

  has_setup = has_text(hostname) || has_text(theme) || has_text(admin_user) ||
              has_text(admin_pass) || has_text(splash);
  if (!has_setup) return 0;

  if (!has_text(hostname) || !has_text(theme) || !has_text(admin_user) ||
      !has_text(admin_pass)) {
    set_error(err, err_len,
              "preseed requires hostname, theme, admin-user and admin-pass");
    return -1;
  }
  if (copy_field(cfg->hostname, sizeof(cfg->hostname), hostname,
                 "hostname too long", err, err_len) != 0) {
    return -1;
  }
  if (copy_field(cfg->theme, sizeof(cfg->theme), theme,
                 "theme too long", err, err_len) != 0) {
    return -1;
  }
  if (copy_field(cfg->admin_username, sizeof(cfg->admin_username), admin_user,
                 "admin user too long", err, err_len) != 0) {
    return -1;
  }
  if (copy_field(cfg->admin_password, sizeof(cfg->admin_password), admin_pass,
                 "admin password too long", err, err_len) != 0) {
    return -1;
  }
  if (parse_splash(splash, 1, &splash_enabled, err, err_len) != 0) {
    return -1;
  }

  cfg->flags |= BOOT_CONFIG_FLAG_HAS_SETUP_DATA;
  cfg->splash_enabled = splash_enabled;
  return 0;
}

#ifndef UNIT_TEST
static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s <output_file> --layout <layout> [--language <language>]\n"
          "          [--volume-key <key>] [--hostname <host>] [--theme <theme>]\n"
          "          [--admin-user <user>] [--admin-pass <pass>] [--splash <0|1>]\n",
          prog);
}

int main(int argc, char *argv[]) {
  const char *outfile = NULL;
  const char *layout = NULL;
  const char *language = NULL;
  const char *volume_key = NULL;
  const char *hostname = NULL;
  const char *theme = NULL;
  const char *admin_user = NULL;
  const char *admin_pass = NULL;
  const char *splash = NULL;
  struct boot_config_sector cfg;
  char err[128];
  FILE *fp = NULL;

  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  outfile = argv[1];
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--layout") == 0 && i + 1 < argc) {
      layout = argv[++i];
    } else if (strcmp(argv[i], "--language") == 0 && i + 1 < argc) {
      language = argv[++i];
    } else if (strcmp(argv[i], "--volume-key") == 0 && i + 1 < argc) {
      volume_key = argv[++i];
    } else if (strcmp(argv[i], "--hostname") == 0 && i + 1 < argc) {
      hostname = argv[++i];
    } else if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
      theme = argv[++i];
    } else if (strcmp(argv[i], "--admin-user") == 0 && i + 1 < argc) {
      admin_user = argv[++i];
    } else if (strcmp(argv[i], "--admin-pass") == 0 && i + 1 < argc) {
      admin_pass = argv[++i];
    } else if (strcmp(argv[i], "--splash") == 0 && i + 1 < argc) {
      splash = argv[++i];
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (!has_text(layout)) {
    usage(argv[0]);
    return 1;
  }

  err[0] = '\0';
  if (gen_boot_config_build(&cfg, layout, language, volume_key, hostname, theme,
                            admin_user, admin_pass, splash, err,
                            sizeof(err)) != 0) {
    fprintf(stderr, "Error: %s\n", err[0] ? err : "failed to build boot config");
    return 1;
  }

  fp = fopen(outfile, "wb");
  if (!fp) {
    perror("fopen");
    return 1;
  }
  if (fwrite(&cfg, 1, sizeof(cfg), fp) != sizeof(cfg)) {
    perror("fwrite");
    fclose(fp);
    return 1;
  }

  fclose(fp);
  printf("Generated boot config at '%s' with layout '%s' and language '%s'\n",
         outfile, cfg.keyboard_layout, cfg.language);
  return 0;
}
#endif
