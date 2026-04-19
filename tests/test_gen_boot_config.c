#include <stdio.h>
#include <string.h>

#include "boot/boot_config.h"

int gen_boot_config_build(struct boot_config_sector *cfg, const char *layout,
                          const char *language, const char *volume_key,
                          const char *hostname, const char *theme,
                          const char *admin_user, const char *admin_pass,
                          const char *splash, char *err, size_t err_len);

static int test_boot_config_defaults(void) {
  struct boot_config_sector cfg;
  char err[128];

  err[0] = '\0';
  if (gen_boot_config_build(&cfg, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                            NULL, err, sizeof(err)) != 0) {
    printf("[FAIL] gen_boot_config defaults rejected: %s\n", err);
    return 1;
  }
  if (cfg.magic != BOOT_CONFIG_MAGIC || cfg.version != BOOT_CONFIG_VERSION) {
    printf("[FAIL] gen_boot_config defaults header mismatch\n");
    return 1;
  }
  if (cfg.flags != 0) {
    printf("[FAIL] gen_boot_config defaults should not set flags\n");
    return 1;
  }
  if (strcmp(cfg.keyboard_layout, "us") != 0 || strcmp(cfg.language, "en") != 0) {
    printf("[FAIL] gen_boot_config defaults expected us/en\n");
    return 1;
  }
  if (cfg.hostname[0] || cfg.theme[0] || cfg.admin_username[0] ||
      cfg.admin_password[0] || cfg.splash_enabled != 0) {
    printf("[FAIL] gen_boot_config defaults should not preseed setup data\n");
    return 1;
  }
  return 0;
}

static int test_boot_config_preseed_complete(void) {
  struct boot_config_sector cfg;
  char err[128];

  err[0] = '\0';
  if (gen_boot_config_build(&cfg, "br-abnt2", "pt-BR", NULL, "capyos-node",
                            "forest", "admin", "secret", "0", err,
                            sizeof(err)) != 0) {
    printf("[FAIL] gen_boot_config preseed rejected: %s\n", err);
    return 1;
  }
  if ((cfg.flags & BOOT_CONFIG_FLAG_HAS_SETUP_DATA) == 0) {
    printf("[FAIL] gen_boot_config preseed missing setup flag\n");
    return 1;
  }
  if (strcmp(cfg.keyboard_layout, "br-abnt2") != 0 ||
      strcmp(cfg.language, "pt-BR") != 0 ||
      strcmp(cfg.hostname, "capyos-node") != 0 ||
      strcmp(cfg.theme, "forest") != 0 ||
      strcmp(cfg.admin_username, "admin") != 0 ||
      strcmp(cfg.admin_password, "secret") != 0) {
    printf("[FAIL] gen_boot_config preseed copied the wrong fields\n");
    return 1;
  }
  if (cfg.splash_enabled != 0) {
    printf("[FAIL] gen_boot_config preseed splash should be disabled\n");
    return 1;
  }
  return 0;
}

static int test_boot_config_preseed_partial_fails(void) {
  struct boot_config_sector cfg;
  char err[128];

  err[0] = '\0';
  if (gen_boot_config_build(&cfg, "us", "en", NULL, "capyos-node", NULL, NULL,
                            NULL, NULL, err, sizeof(err)) == 0) {
    printf("[FAIL] gen_boot_config accepted partial setup data\n");
    return 1;
  }
  if (strstr(err, "preseed requires") == NULL) {
    printf("[FAIL] gen_boot_config partial setup error was not specific\n");
    return 1;
  }
  return 0;
}

int run_gen_boot_config_tests(void) {
  int fails = 0;
  fails += test_boot_config_defaults();
  fails += test_boot_config_preseed_complete();
  fails += test_boot_config_preseed_partial_fails();
  if (fails == 0) printf("[PASS] gen_boot_config\n");
  return fails;
}
