/* kernel.c: main runtime entry after multiboot handoff.
 * Initializes CPU tables/devices, mounts storage, then runs login + CLI. */
/* removed: legacy x86 gdt.h */
/* removed: legacy x86 idt.h */
#include "drivers/irq.h"
#include "drivers/io.h"
#include "boot/boot_writer.h"
#include "core/system_init.h"
#include "core/user.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/timer/pit.h"
#include "drivers/video/vga.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/ramdisk.h"
#include "fs/storage/partition.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "security/crypt.h"
#include "shell/shell.h"
#include <stddef.h>
#include <stdint.h>

static struct super_block root_sb;
static const uint8_t g_disk_salt[16] = {0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53,
                                        0x2d, 0x46, 0x53, 0x2d, 0x53, 0x61,
                                        0x6c, 0x74, 0x21, 0x00};
static const uint32_t g_kdf_iterations = 16000;

static void memzero(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static int mount_capyfs_root(struct block_device *crypt_dev) {
  if (mount_capyfs(crypt_dev, &root_sb) != 0) {
    return -1;
  }
  if (vfs_mount_root(&root_sb) != 0) {
    return -1;
  }
  vga_write("CAPYFS montado em / (dados cifrados)\n");
  return 0;
}

static void kernel_log_process_begin(const char *name) {
  vga_write("Iniciando processo de ");
  vga_write(name);
  vga_write("...");
  vga_newline();
}

static void kernel_log_process_begin_success(const char *name) {
  vga_write("Processo ");
  vga_write(name);
  vga_write(" iniciado com sucesso...");
  vga_newline();
}

static void kernel_log_process_progress(const char *name) {
  vga_write("Processo ");
  vga_write(name);
  vga_write(" em andamento.");
  vga_newline();
}

static void kernel_log_process_conclude(const char *name) {
  vga_write("Processo ");
  vga_write(name);
  vga_write(" concluido.");
  vga_newline();
}

static void kernel_log_process_finalize(const char *name) {
  vga_write("Finalizando processo ");
  vga_write(name);
  vga_write("...");
  vga_newline();
}

static void kernel_log_process_finalize_success(const char *name) {
  vga_write("Processo ");
  vga_write(name);
  vga_write(" finalizado com sucesso.");
  vga_newline();
}

static void kernel_log_dependency_wait(const char *dependency,
                                       const char *target) {
  vga_write("Aguardando processo ");
  vga_write(dependency);
  vga_write(" para iniciar o processo ");
  vga_write(target);
  vga_write("...");
  vga_newline();
}

static void kernel_log_process_error(const char *name, const char *reason) {
  vga_write("[erro] Processo ");
  vga_write(name);
  vga_write(" falhou");
  if (reason) {
    vga_write(": ");
    vga_write(reason);
  }
  vga_write(".");
  vga_newline();
}

static int probe_block_zeroed(struct block_device *dev, uint32_t lba,
                              uint8_t *buf) {
  if (lba >= dev->block_count)
    return 1; // fora do range: nÃƒÆ’Ã‚Â£o conclui nada
  size_t bs = dev->block_size;
  // Buffer provided by caller
  int all_zero = 1;
  if (block_device_read(dev, lba, buf) != 0) {
    all_zero = 0; // falha de leitura
  } else {
    for (size_t i = 0; i < bs; ++i) {
      if (buf[i] != 0) {
        all_zero = 0;
        break;
      }
    }
  }
  return all_zero;
}

static int device_is_blank(struct block_device *dev) {
  if (!dev || dev->block_count == 0 || dev->block_size == 0)
    return 1;

  uint8_t *buf = (uint8_t *)kalloc(dev->block_size);
  if (!buf)
    return 0; // Allocation failure, assume not blank to be safe

  // Verifica mÃƒÆ’Ã‚Âºltiplos LBAs, incluindo regiÃƒÆ’Ã‚Â£o apÃƒÆ’Ã‚Â³s o offset de formataÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â£o (128)
  uint32_t samples[9];
  size_t n = 0;
  samples[n++] = 0;
  if (dev->block_count > 1)
    samples[n++] = 1;
  if (dev->block_count > 2)
    samples[n++] = 2;
  if (dev->block_count > 128) {
    samples[n++] = 127;
    samples[n++] = 128;
    samples[n++] = 129;
  }
  if (dev->block_count > 256)
    samples[n++] = 256;
  if (dev->block_count > 4)
    samples[n++] = dev->block_count / 2;
  if (dev->block_count > 0)
    samples[n++] = dev->block_count - 1;

  for (size_t i = 0; i < n; ++i) {
    if (!probe_block_zeroed(dev, samples[i], buf)) {
      kfree(buf);
      return 0; // encontrou dado nÃƒÆ’Ã‚Â£o-zero
    }
  }
  kfree(buf);
  return 1; // todas as amostras zero
}

// Seleciona o backend correto: lÃƒÆ’Ã‚Âª a MBR no disco cru (512B) e expÃƒÆ’Ã‚Âµe a
// partiÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â£o de dados (tipicamente partiÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â£o 2). Envolve em blocos de 4096B para
// CAPYFS.
static struct block_device *pick_root_device(struct block_device *raw_ata) {
  if (!raw_ata) {
    vga_write("[boot] ERRO: dispositivo ATA nulo\n");
    return NULL;
  }

  // Log device info
  vga_write("[boot] usando dispositivo: ");
  vga_write(raw_ata->name ? raw_ata->name : "(sem nome)");
  vga_write("\n");

  // First read MBR to check signature
  uint8_t mbr_buf[512];
  if (block_device_read(raw_ata, 0, mbr_buf) != 0) {
    vga_write("[boot] falha ao ler MBR do dispositivo\n");
    // Fallback to whole disk if MBR read fails
    struct block_device *chunked =
        block_chunked_wrap(raw_ata, CAPYFS_BLOCK_SIZE);
    return chunked ? chunked : raw_ata;
  }

  // Check MBR signature
  if (mbr_buf[510] != 0x55 || mbr_buf[511] != 0xAA) {
    vga_write("[boot] MBR sem assinatura valida (esperado 0x55AA)\n");
    vga_write("[boot] tentando usar disco inteiro como volume\n");
    struct block_device *chunked =
        block_chunked_wrap(raw_ata, CAPYFS_BLOCK_SIZE);
    return chunked ? chunked : raw_ata;
  }

  vga_write("[boot] MBR com assinatura valida detectada\n");

  struct block_device *slice = NULL;
  struct mbr_partition data_part;
  struct mbr_partition boot_part;

  // Try partition 2 first (data partition in CapyOS layout)
  if (mbr_read_partition(raw_ata, 1, &data_part) == 0) {
    vga_write("[boot] particao 2 encontrada: LBA=");
    // Print LBA start
    char lba_buf[12];
    uint32_t lba = data_part.lba_start;
    int pos = 0;
    if (lba == 0) {
      lba_buf[pos++] = '0';
    } else {
      char rev[12];
      int ri = 0;
      while (lba > 0) {
        rev[ri++] = '0' + (lba % 10);
        lba /= 10;
      }
      while (ri > 0) {
        lba_buf[pos++] = rev[--ri];
      }
    }
    lba_buf[pos] = '\0';
    vga_write(lba_buf);
    vga_write(", setores=");
    // Print sector count
    uint32_t secs = data_part.sector_count;
    pos = 0;
    if (secs == 0) {
      lba_buf[pos++] = '0';
    } else {
      char rev[12];
      int ri = 0;
      while (secs > 0) {
        rev[ri++] = '0' + (secs % 10);
        secs /= 10;
      }
      while (ri > 0) {
        lba_buf[pos++] = rev[--ri];
      }
    }
    lba_buf[pos] = '\0';
    vga_write(lba_buf);
    vga_write("\n");

    slice =
        block_offset_wrap(raw_ata, data_part.lba_start, data_part.sector_count);
    if (slice) {
      vga_write("[boot] usando particao 2 como volume CAPYFS\n");
    } else {
      vga_write("[boot] falha ao criar wrapper para particao 2\n");
    }
  } else {
    vga_write("[boot] particao 2 nao encontrada na MBR\n");
  }

  // If partition 2 not available, try partition 1 (may be data if only one
  // partition)
  if (!slice && mbr_read_partition(raw_ata, 0, &boot_part) == 0) {
    vga_write("[boot] tentando particao 1 como fallback\n");
    slice =
        block_offset_wrap(raw_ata, boot_part.lba_start, boot_part.sector_count);
    if (slice) {
      vga_write("[boot] usando particao 1\n");
    }
  }

  // Fallback to whole disk
  if (!slice) {
    vga_write("[boot] nenhuma particao valida; usando disco inteiro\n");
    slice = raw_ata;
  }

  struct block_device *chunked = block_chunked_wrap(slice, CAPYFS_BLOCK_SIZE);
  if (chunked) {
    return chunked;
  }
  vga_write("[boot] aviso: nao foi possivel criar wrapper 4096B\n");
  return slice;
}

/* format_and_mount removido: formataÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â£o agora ÃƒÆ’Ã‚Â© tarefa do instalador (NGIS). */

struct multiboot_info {
  uint32_t flags;
  uint32_t mem_lower;
  uint32_t mem_upper;
  uint32_t boot_device;
  uint32_t cmdline; // char* (physical addr)
};

/* Sem modo de instalacao embutido. */

void kernel_main(uint32_t mb_magic, uint32_t mb_info_ptr) {
  /* DEBUG: Write 'Y' to VGA to confirm kernel entry */
  volatile unsigned short *vga = (unsigned short *)0xB8000;
  vga[1] = 0x2F59; /* 'Y' in White on Green */

  (void)mb_magic;
  (void)mb_info_ptr;
  char line[TTY_BUFFER_MAX];
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  int fs_ready = 0;

  vga_init();
  vga_write("CapyOS 1 - Versao Singularity esta rodando!\n\n");
  vga_write("Ola Mundo!\n\n");

  gdt_init();
  idt_install();
  pic_remap(0x20, 0x28);
  pic_set_mask(0xFC, 0xFF);
  pit_init(100);

  kinit();
  buffer_cache_init();
  vfs_init();
  ramdisk_init(256);
  // Initialize ATA if present
  sti(); // Enable interrupts before ATA init
  extern void ata_init(void);
  extern struct block_device *ata_primary_device(void);
  ata_init();

  tty_init();
  keyboard_init();

  /* Instalacao separada: sem suporte a mode=install no kernel principal. */

  // Move sti() up

  struct block_device *root_dev = NULL;
  struct block_device *ata = ata_primary_device();
  if (ata) {
    root_dev = pick_root_device(ata);
  }
  if (!root_dev) {
    root_dev = ramdisk_device();
  }
  if (!root_dev) {
    vga_write("RAMDISK indisponivel\n");
  } else {
    int blank_device = device_is_blank(root_dev);
    if (blank_device) {
      vga_write("Dispositivo vazio detectado.\n");
      vga_write("Inicie pelo instalador (ISO) para formatar e instalar.\n");
    } else {
      // NEW: Try to load boot configuration (keyboard layout) from LBA 1
      struct boot_config_sector cfg;
      if (block_device_read(root_dev, BOOT_CONFIG_LBA, (uint8_t *)&cfg) == 0) {
        if (cfg.magic == BOOT_CONFIG_MAGIC) {
          char layout[17];
          // Ensure null termination
          int i;
          for (i = 0; i < 16 && cfg.keyboard_layout[i]; i++) {
            layout[i] = cfg.keyboard_layout[i];
          }
          layout[i] = '\0';
          if (keyboard_set_layout_by_name(layout) == 0) {
            vga_write("Layout de teclado carregado do setor de boot: ");
            vga_write(layout);
            vga_newline();
          }
        }
      }

      // Fluxo de volume cifrado existente: obrigatorio autenticar, sem formatar
      vga_write("Volume cifrado detectado. Informe a senha do CAPYFS.\n");
      while (!fs_ready) {
        tty_set_prompt("Senha: ");
        tty_set_echo_mask('*');
        tty_show_prompt();
        size_t pass_len = tty_readline(line, sizeof(line));
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        if (pass_len == 0) {
          vga_write("Senha obrigatoria.\n");
          continue;
        }
        crypt_derive_xts_keys(line, g_disk_salt, sizeof(g_disk_salt),
                              g_kdf_iterations, key1, key2);
        memzero(line, sizeof(line));
        struct block_device *crypt_dev = crypt_init(root_dev, key1, key2);
        if (!crypt_dev || crypt_dev == root_dev) {
          vga_write(
              "Falha ao iniciar camada criptografica (volume inseguro).\n");
          continue;
        }
        if (mount_capyfs_root(crypt_dev) == 0) {
          fs_ready = 1;
        } else {
          vga_write("Senha incorreta. Tente novamente.\n");
          crypt_free(crypt_dev);
        }
        memzero(key1, sizeof(key1));
        memzero(key2, sizeof(key2));
      }
    }
  }

  memzero(key1, sizeof(key1));
  memzero(key2, sizeof(key2));

  struct system_settings settings;
  const char *proc_load_initial = "carregamento das configuracoes iniciais";
  kernel_log_process_begin(proc_load_initial);
  kernel_log_process_begin_success(proc_load_initial);
  if (system_load_settings(&settings) == 0) {
    kernel_log_process_progress(proc_load_initial);
    kernel_log_process_conclude(proc_load_initial);
    kernel_log_process_finalize(proc_load_initial);
    kernel_log_process_finalize_success(proc_load_initial);
  } else {
    kernel_log_process_error(proc_load_initial,
                             "config.ini indisponivel, usando valores padrao.");
    kernel_log_process_finalize(proc_load_initial);
  }

  const char *proc_keyboard = "aplicacao do layout do teclado";
  kernel_log_dependency_wait(proc_load_initial, proc_keyboard);
  kernel_log_process_begin(proc_keyboard);
  kernel_log_process_begin_success(proc_keyboard);
  system_apply_keyboard_layout(&settings);
  kernel_log_process_progress(proc_keyboard);
  kernel_log_process_conclude(proc_keyboard);
  kernel_log_process_finalize(proc_keyboard);
  kernel_log_process_finalize_success(proc_keyboard);

  const char *proc_theme_initial = "aplicacao do tema inicial";
  kernel_log_dependency_wait(proc_load_initial, proc_theme_initial);
  kernel_log_process_begin(proc_theme_initial);
  kernel_log_process_begin_success(proc_theme_initial);
  system_apply_theme(&settings);
  kernel_log_process_progress(proc_theme_initial);
  kernel_log_process_conclude(proc_theme_initial);
  kernel_log_process_finalize(proc_theme_initial);
  kernel_log_process_finalize_success(proc_theme_initial);

  struct session_context session;
  const char *proc_session = "preparacao da sessao";
  kernel_log_dependency_wait(proc_theme_initial, proc_session);
  kernel_log_process_begin(proc_session);
  kernel_log_process_begin_success(proc_session);
  session_reset(&session);
  kernel_log_process_progress(proc_session);
  kernel_log_process_conclude(proc_session);
  kernel_log_process_finalize(proc_session);
  kernel_log_process_finalize_success(proc_session);

  if (fs_ready) {
    vga_write("Sistema pronto.\n");

    const char *proc_splash = "exibicao do splash";
    kernel_log_dependency_wait(proc_session, proc_splash);
    kernel_log_process_begin(proc_splash);
    kernel_log_process_begin_success(proc_splash);
    system_show_splash(&settings);
    kernel_log_process_progress(proc_splash);
    kernel_log_process_conclude(proc_splash);
    kernel_log_process_finalize(proc_splash);
    kernel_log_process_finalize_success(proc_splash);

    const char *proc_login = "autenticacao de usuario";
    // Verificacao de acesso ao banco de usuarios antes do prompt
    struct file *dbtest = vfs_open(USER_DB_PATH, VFS_OPEN_READ);
    if (dbtest) {
      vga_write("[db] USER_DB acessivel.\n");
      vfs_close(dbtest);
    } else {
      vga_write("[db] USER_DB indisponivel (\"/etc/users.db\").\n");
    }
    int keep_auth = 1;
    while (keep_auth) {
      kernel_log_dependency_wait(proc_splash, proc_login);
      kernel_log_process_begin(proc_login);
      kernel_log_process_begin_success(proc_login);
      if (system_login(&session, &settings) == 0) {
        kernel_log_process_progress(proc_login);
        kernel_log_process_conclude(proc_login);
        kernel_log_process_finalize(proc_login);
        kernel_log_process_finalize_success(proc_login);

        const char *proc_cli = "inicializacao do CapyCLI";
        kernel_log_dependency_wait(proc_login, proc_cli);
        kernel_log_process_begin(proc_cli);
        kernel_log_process_begin_success(proc_cli);
        enum shell_result cli_result = shell_run(&session, &settings);
        kernel_log_process_progress(proc_cli);
        kernel_log_process_conclude(proc_cli);
        kernel_log_process_finalize(proc_cli);
        kernel_log_process_finalize_success(proc_cli);

        if (cli_result == SHELL_RESULT_LOGOUT) {
          const char *proc_standby = "standby de autenticacao";
          kernel_log_process_begin(proc_standby);
          kernel_log_process_begin_success(proc_standby);
          kernel_log_process_progress(proc_standby);
          kernel_log_process_conclude(proc_standby);
          kernel_log_process_finalize(proc_standby);
          kernel_log_process_finalize_success(proc_standby);
          session_reset(&session);
          vga_clear();
          kernel_log_dependency_wait(proc_standby, proc_login);
          continue;
        }
        keep_auth = 0;
      } else {
        kernel_log_process_error(proc_login, "falha na autenticacao.");
        kernel_log_process_finalize(proc_login);
        keep_auth = 0;
      }
    }
  } else {
    vga_write("Falha ao localizar ou montar volume CAPYFS.\n");
    vga_write("Reinicie e inicialize a partir da ISO do instalador para "
              "preparar o disco.\n");
    while (1) {
      __asm__ volatile("hlt");
    }
  }

  while (1) {
    __asm__ volatile("hlt");
  }
}
