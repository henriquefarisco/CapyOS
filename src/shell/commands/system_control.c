#include "shell/commands.h"
#include "shell/core.h"

#include "arch/x86/hw/io.h"
#include "core/system_init.h"
#include "drivers/acpi/acpi.h"
#include "drivers/input/keyboard.h"
#include "fs/buffer.h"
#include "fs/vfs.h"

static int cmd_config_keyboard(struct shell_context *ctx, int argc,
                               char **argv) {
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: config-keyboard [layout]\n"
                "Sem argumentos mostra os layouts disponiveis.\n"
                "Com um argumento (ex.: config-keyboard br-abnt2) altera o "
                "layout ativo.\n"
                "Subcomandos: list, show.\n");
    return 0;
  }
  if (argc < 2 || shell_string_equal(argv[1], "list")) {
    shell_print("Layout atual: ");
    const char *cur = keyboard_current_layout();
    shell_print(cur ? cur : "(desconhecido)");
    shell_newline();
    shell_print("Layouts disponiveis:\n");
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
      shell_print(" - ");
      shell_print(keyboard_layout_name(i));
      shell_print(" : ");
      shell_print(keyboard_layout_description(i));
      shell_newline();
    }
    return 0;
  }
  if (shell_string_equal(argv[1], "show")) {
    shell_print("Layout atual: ");
    const char *cur = keyboard_current_layout();
    shell_print(cur ? cur : "(desconhecido)");
    shell_newline();
    return 0;
  }
  if (keyboard_set_layout_by_name(argv[1]) != 0) {
    shell_print_error("layout desconhecido");
    shell_suggest_help("config-keyboard");
    return -1;
  }
  if (system_save_keyboard_layout(argv[1]) != 0) {
    shell_print_ok("layout atualizado");
    shell_print("Aviso: nao foi possivel salvar em /system/config.ini.\n");
  } else {
    shell_print_ok("layout atualizado e salvo");
  }
  return 0;
}

static void sync_and_flush(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    buffer_cache_sync(root->bdev);
  }
}

/* Pequeno delay para I/O. */
static void io_wait(void) { outb(0x80, 0); }

static void do_hard_reboot(void) {
  sync_and_flush();
  shell_print("Reiniciando...\n");
  cli();

  /* Metodo 1: Reset via controlador de teclado 8042. */
  uint8_t good = 0x02;
  while (good & 0x02)
    good = inb(0x64);
  outb(0x64, 0xFE);
  io_wait();

  /* Metodo 2: Triple fault (carrega IDT nulo e dispara interrupcao). */
  struct {
    uint16_t limit;
    uint32_t base;
  } __attribute__((packed)) null_idt = {0, 0};
  __asm__ volatile("lidt %0" : : "m"(null_idt));
  __asm__ volatile("int $0x03");

  while (1) {
    hlt();
  }
}

static void do_power_off(void) {
  sync_and_flush();
  shell_print("Desligando...\n");
  cli();

  /* Metodo 1: ACPI shutdown (S5 state). */
  acpi_shutdown();

  /* Fallback: QEMU/Bochs debug exit ports. */
  outw(0x604, 0x2000);  /* QEMU isa-debug-exit (newer) */
  outw(0xB004, 0x2000); /* Bochs/older QEMU */

  /* Last resort: halt. */
  while (1) {
    hlt();
  }
}

static int cmd_shutdown_reboot(struct shell_context *ctx, int argc,
                               char **argv) {
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: shutdown-reboot\nReinicia o sistema de forma controlada "
                "(sincroniza buffers).");
    shell_newline();
    return 0;
  }
  do_hard_reboot();
  return 0;
}

static int cmd_shutdown_off(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  (void)argv;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: shutdown-off\nDesliga o sistema (halt) apos sincronizar "
                "buffers.");
    shell_newline();
    return 0;
  }
  do_power_off();
  return 0;
}

static int cmd_do_sync(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx;
  if (shell_help_requested(argc, argv)) {
    shell_print("Uso: do-sync\nForca a gravacao do buffer de disco.");
    shell_newline();
    return 0;
  }
  (void)argc;
  (void)argv;
  struct super_block *root = vfs_root();
  if (!root || !root->bdev) {
    shell_print_error("sem dispositivo");
    return -1;
  }
  buffer_cache_sync(root->bdev);
  shell_print_ok("buffers sincronizados");
  return 0;
}

static struct shell_command g_system_control_commands[4];
static int g_system_control_commands_initialized = 0;

static void init_system_control_commands(void) {
  if (g_system_control_commands_initialized) {
    return;
  }
  g_system_control_commands[0].name = "config-keyboard";
  g_system_control_commands[0].handler = cmd_config_keyboard;
  g_system_control_commands[1].name = "shutdown-reboot";
  g_system_control_commands[1].handler = cmd_shutdown_reboot;
  g_system_control_commands[2].name = "shutdown-off";
  g_system_control_commands[2].handler = cmd_shutdown_off;
  g_system_control_commands[3].name = "do-sync";
  g_system_control_commands[3].handler = cmd_do_sync;
  g_system_control_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_control(size_t *count) {
  init_system_control_commands();
  if (count) {
    *count = 4;
  }
  return g_system_control_commands;
}
