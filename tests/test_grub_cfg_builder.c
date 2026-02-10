#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grub_cfg_builder.h"

static int test_build_includes_console_and_entries(void) {
  char buffer[1024];
  struct grub_menu_entry entries[2] = {
      {.title = "Installer", .multiboot_path = "/boot/installer.bin"},
      {.title = "Kernel", .multiboot_path = "/boot/noiros.bin"},
  };
  struct grub_cfg_options opts = {
      .timeout_seconds = 2,
      .default_entry = 0,
      .enable_serial = 1,
      .serial_baud = 57600,
      .force_text_mode = 1,
  };

  if (grub_cfg_build(&opts, entries, 2, buffer, sizeof(buffer)) != 0) {
    printf("[grub] build falhou com argumentos validos\n");
    return 1;
  }
  if (!strstr(buffer, "terminal_output console serial")) {
    printf("[grub] terminal_output ausente\n");
    return 1;
  }
  if (!strstr(buffer, "set gfxpayload=text")) {
    printf("[grub] gfxpayload=text ausente\n");
    return 1;
  }
  if (!strstr(buffer, "Installer") || !strstr(buffer, "/boot/noiros.bin")) {
    printf("[grub] entradas nao renderizadas\n");
    return 1;
  }
  if (!strstr(buffer, "speed=57600")) {
    printf("[grub] baud configurado nao aplicado\n");
    return 1;
  }
  return 0;
}

static int test_build_rejects_invalid_default_or_buffer(void) {
  char buffer[64];
  struct grub_menu_entry entry = {.title = "Kernel",
                                  .multiboot_path = "/boot/noiros.bin"};
  struct grub_cfg_options opts = {
      .timeout_seconds = 1,
      .default_entry = 1, // invalido para 1 entrada
      .enable_serial = 0,
      .serial_baud = 9600,
      .force_text_mode = 0,
  };

  if (grub_cfg_build(&opts, &entry, 1, buffer, sizeof(buffer)) == 0) {
    printf("[grub] aceitou default_entry fora do range\n");
    return 1;
  }

  opts.default_entry = 0;
  if (grub_cfg_build(&opts, &entry, 1, buffer, 8) == 0) {
    printf("[grub] aceitou buffer insuficiente\n");
    return 1;
  }
  return 0;
}

static int test_write_file_creates_content(void) {
  char template[] = "/tmp/grubcfgXXXXXX";
  int fd = mkstemp(template);
  if (fd < 0) {
    printf("[grub] mkstemp falhou\n");
    return 1;
  }
  close(fd);

  struct grub_menu_entry entry = {.title = "Kernel",
                                  .multiboot_path = "/boot/noiros.bin"};
  struct grub_cfg_options opts = {
      .timeout_seconds = 1,
      .default_entry = 0,
      .enable_serial = 1,
      .serial_baud = 115200,
      .force_text_mode = 1,
  };

  int rc = grub_cfg_write_file(template, &opts, &entry, 1);
  if (rc != 0) {
    printf("[grub] grub_cfg_write_file retornou erro\n");
    unlink(template);
    return 1;
  }

  FILE *f = fopen(template, "r");
  if (!f) {
    printf("[grub] falha ao reabrir arquivo\n");
    unlink(template);
    return 1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    printf("[grub] falha ao buscar tamanho do arquivo\n");
    fclose(f);
    unlink(template);
    return 1;
  }
  long len = ftell(f);
  if (len < 0 || len > 4096) {
    printf("[grub] tamanho inesperado do grub.cfg (%ld)\n", len);
    fclose(f);
    unlink(template);
    return 1;
  }
  rewind(f);

  char *contents = (char *)calloc((size_t)len + 1, 1);
  if (!contents) {
    printf("[grub] OOM ao alocar buffer de leitura\n");
    fclose(f);
    unlink(template);
    return 1;
  }

  size_t read = fread(contents, 1, (size_t)len, f);
  fclose(f);
  unlink(template);

  if (read == 0 || !strstr(contents, "terminal_input console")) {
    printf("[grub] conteudo nao contem terminal_input (read=%zu)\n", read);
    printf("[grub] dump: %s\n", contents);
    free(contents);
    return 1;
  }
  if (!strstr(contents, "menuentry \"Kernel\"")) {
    printf("[grub] menuentry kernel nao encontrado\n");
    free(contents);
    return 1;
  }
  free(contents);
  return 0;
}

int run_grub_cfg_builder_tests(void) {
  int fails = 0;
  fails += test_build_includes_console_and_entries();
  fails += test_build_rejects_invalid_default_or_buffer();
  fails += test_write_file_creates_content();
  if (fails == 0) {
    printf("[tests] grub_cfg_builder OK\n");
  }
  return fails;
}
