#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot/boot_config.h"

void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s <output_file> --layout <layout_name> [--language <language>]\n",
          prog);
}

int main(int argc, char *argv[]) {
  if (argc != 4 && argc != 6) {
    usage(argv[0]);
    return 1;
  }

  const char *outfile = argv[1];
  const char *layout_arg = argv[2];
  const char *layout_name = argv[3];
  const char *language = "en";

  if (strcmp(layout_arg, "--layout") != 0) {
    usage(argv[0]);
    return 1;
  }

  if (argc == 6) {
    if (strcmp(argv[4], "--language") != 0) {
      usage(argv[0]);
      return 1;
    }
    language = argv[5];
  }

  if (strlen(layout_name) >= 16) {
    fprintf(stderr, "Error: Layout name too long (max 15 chars)\n");
    return 1;
  }
  if (strlen(language) >= 16) {
    fprintf(stderr, "Error: Language too long (max 15 chars)\n");
    return 1;
  }

  struct boot_config_sector cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.magic = BOOT_CONFIG_MAGIC;
  cfg.version = BOOT_CONFIG_VERSION;
  strcpy(cfg.keyboard_layout, layout_name);
  strcpy(cfg.language, language);

  FILE *f = fopen(outfile, "wb");
  if (!f) {
    perror("fopen");
    return 1;
  }

  if (fwrite(&cfg, 1, sizeof(cfg), f) != sizeof(cfg)) {
    perror("fwrite");
    fclose(f);
    return 1;
  }

  fclose(f);
  printf("Generated boot config at '%s' with layout '%s' and language '%s'\n",
         outfile, layout_name, language);
  return 0;
}
