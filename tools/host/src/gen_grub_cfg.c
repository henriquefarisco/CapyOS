#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grub_cfg_builder.h"

static int build_iso_cfg(const char *out_path) {
  const struct grub_menu_entry entries[] = {
      {.title = "CAPYOS Guided Installation System",
       .multiboot_path = "/boot/installer.bin"},
      {.title = "CAPYOS (boot via ISO + disco instalado)",
       .multiboot_path = "/boot/capyos.bin"},
  };
  const struct grub_cfg_options opts = {
      .timeout_seconds = 1,
      .default_entry = 0,
      .enable_serial = 1,
      .serial_baud = 115200,
      .force_text_mode = 1,
  };
  return grub_cfg_write_file(out_path, &opts, entries,
                             sizeof(entries) / sizeof(entries[0]));
}

static int build_disk_cfg(const char *out_path) {
  const struct grub_menu_entry entries[] = {
      {.title = "CAPYOS", .multiboot_path = "/boot/capyos.bin"},
  };
  const struct grub_cfg_options opts = {
      .timeout_seconds = 3,
      .default_entry = 0,
      .enable_serial = 1,
      .serial_baud = 115200,
      .force_text_mode = 1,
  };
  return grub_cfg_write_file(out_path, &opts, entries,
                             sizeof(entries) / sizeof(entries[0]));
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Uso: %s <saida-grub.cfg> [iso|disk]\n", argv[0]);
    return 1;
  }
  const char *out_path = argv[1];
  const char *mode = (argc == 3) ? argv[2] : "iso";

  int rc = -1;
  if (strcmp(mode, "iso") == 0) {
    rc = build_iso_cfg(out_path);
  } else if (strcmp(mode, "disk") == 0) {
    rc = build_disk_cfg(out_path);
  } else {
    fprintf(stderr, "Modo invalido: %s (use iso|disk)\n", mode);
    return 1;
  }

  if (rc != 0) {
    fprintf(stderr, "Falha ao gerar grub.cfg em %s\n", out_path);
    return 1;
  }
  return 0;
}
