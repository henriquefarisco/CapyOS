#include <stddef.h>
#include <stdint.h>

#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/isr.h"
#include "arch/x86/hw/io.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/timer/pit.h"
#include "drivers/video/vga.h"

#include "memory/kmem.h"

#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/ramdisk.h"
#include "fs/storage/partition.h"
#include "fs/vfs.h"

#include "security/crypt.h"

#include "boot/boot_writer.h"

#include "core/system_init.h"

// Capy Guided Installation System - instalador dedicado

struct dev_choice {
  struct block_device *dev;
  const char *name;
  uint64_t bytes;
};

static struct super_block root_sb;

static const uint8_t g_disk_salt[16] = {0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53,
                                        0x2d, 0x46, 0x53, 0x2d, 0x53, 0x61,
                                        0x6c, 0x74, 0x21, 0x00};
static const uint32_t g_kdf_iterations = 16000;
static const uint32_t CAPYFS_DATA_MIN_SECTORS =
    32768u; // ~16MiB em setores de 512B

static void memzero(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--)
    *p++ = 0;
}

/* Pequeno delay para I/O. */
static void io_wait(void) { outb(0x80, 0); }

/* Espera o usuario pressionar qualquer tecla e reinicia. */
static void wait_and_reboot(void) {
  vga_write("\nPressione qualquer tecla para reiniciar...\n");
  keyboard_wait_any();
  /* Sincroniza buffers antes de reiniciar */
  struct super_block *rsb = vfs_root();
  if (rsb && rsb->bdev)
    buffer_cache_sync(rsb->bdev);

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

static size_t utoa10(uint32_t v, char *dst) {
  char t[10];
  size_t n = 0;
  if (!v) {
    dst[0] = '0';
    return 1;
  }
  while (v && n < sizeof(t)) {
    t[n++] = (char)('0' + (v % 10));
    v /= 10;
  }
  for (size_t i = 0; i < n; ++i)
    dst[i] = t[n - 1 - i];
  return n;
}

static int mount_capyfs_root(struct block_device *crypt_dev) {
  if (mount_capyfs(crypt_dev, &root_sb) != 0)
    return -1;
  if (vfs_mount_root(&root_sb) != 0)
    return -1;
  vga_write("CAPYFS montado em / (dados cifrados)\n");
  return 0;
}

static int format_progress_complete = 0;
static void format_progress(const char *stage, uint32_t percent) {
  if (!stage)
    stage = "";
  if (percent > 100)
    percent = 100;
  char numbuf[10];
  size_t numlen = utoa10(percent, numbuf);
  numbuf[numlen] = '\0';
  vga_write("Formatacao: ");
  vga_write(numbuf);
  vga_write("% ");
  vga_write(stage);
  vga_newline();
  /* menor spin para evitar longas esperas ocupando CPU e "congelando" a saÃƒÆ’Ã‚Â­da
     VGA em emuladores mantÃƒÆ’Ã‚Â©m um pequeno atraso visual entre atualizaÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Âµes de
     progresso, sem travar tanto o sistema */
  for (volatile uint32_t spin = 0; spin < 20000; ++spin) {
    __asm__ volatile("");
  }
  if (percent >= 100)
    format_progress_complete = 1;
}

static int format_and_mount(struct block_device *crypt_dev) {
  vga_write("CAPYFS indisponivel. Iniciando formatacao...\n");
  format_progress_complete = 0;
  int fmt =
      capyfs_format(crypt_dev, 128, crypt_dev->block_count, format_progress);
  if (!format_progress_complete)
    vga_write("\n");
  if (fmt != 0) {
    vga_write("Falha ao formatar CAPYFS\n");
    return -1;
  }
  if (mount_capyfs_root(crypt_dev) != 0) {
    vga_write("Falha ao montar CAPYFS apos formatacao\n");
    return -1;
  }
  return 0;
}

static void human_size(uint64_t bytes, char *buf, size_t buflen) {
  const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int ui = 0;
  int shift = 0;
  while (ui < 4 && (bytes >> (shift + 10)) > 0) {
    ui++;
    shift += 10;
  }

  uint64_t whole = (shift == 0) ? bytes : (bytes >> shift);
  uint64_t rem = (shift == 0) ? 0 : (bytes & ((1ULL << shift) - 1));
  uint64_t frac = (shift == 0) ? 0 : ((rem * 100ULL) >> shift);

  char tmp[32];
  int ti = 0;
  if (!whole) {
    tmp[ti++] = '0';
  } else {
    char r[32];
    int ri = 0;
    uint32_t t = (uint32_t)whole;
    while (t) {
      r[ri++] = (char)('0' + (t % 10u));
      t /= 10u;
    }
    while (ri) {
      tmp[ti++] = r[--ri];
    }
  }
  tmp[ti] = '\0';

  size_t i = 0, j = 0;
  while (tmp[j] && i + 1 < buflen)
    buf[i++] = tmp[j++];
  if (ui > 0 && i + 1 < buflen) {
    buf[i++] = '.';
    uint32_t f = (uint32_t)frac;
    char d1 = (char)('0' + ((f / 10u) % 10u));
    char d2 = (char)('0' + (f % 10u));
    if (i + 1 < buflen)
      buf[i++] = d1;
    if (i + 1 < buflen)
      buf[i++] = d2;
  }
  if (i + 1 < buflen) {
    buf[i++] = ' ';
  }
  const char *u = units[ui];
  j = 0;
  while (u[j] && i + 1 < buflen)
    buf[i++] = u[j++];
  buf[i] = '\0';
}

static void copy_string(char *dst, size_t dst_len, const char *src) {
  if (!dst || dst_len == 0)
    return;
  size_t i = 0;
  if (src) {
    while (src[i] && i < dst_len - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int wizard_prompt_number(const char *label, uint32_t *out_val,
                                uint32_t min, uint32_t max, uint32_t def) {
  char buf[128];
  while (1) {
    tty_set_prompt(label);
    tty_set_echo(1);
    tty_show_prompt();
    size_t l = tty_readline(buf, sizeof(buf));
    if (l == 0) {
      *out_val = def;
      return 0;
    }
    uint32_t v = 0;
    int ok = 1;
    for (size_t i = 0; i < l; i++) {
      char c = buf[i];
      if (c < '0' || c > '9') {
        ok = 0;
        break;
      }
      v = v * 10 + (uint32_t)(c - '0');
    }
    if (!ok) {
      *out_val = def;
      return 0;
    }
    if (v < min || v > max) {
      vga_write("Valor fora do intervalo.\n");
      continue;
    }
    *out_val = v;
    return 0;
  }
}

void kernel_main(uint32_t mb_magic, uint32_t mb_info_ptr) {
  (void)mb_magic;
  (void)mb_info_ptr;
  vga_init();
  vga_write("Capy Guided Installation System\n\n");
  gdt_init();
  idt_install();
  pic_remap(0x20, 0x28);
  pic_set_mask(0xFC, 0xFF);
  pit_init(100);
  sti();
  kinit();
  buffer_cache_init();
  vfs_init();
  ramdisk_init(256);
  extern void ata_init(void);
  ata_init();
  tty_init();
  keyboard_init();

  // Escolha de layout antes de qualquer prompt (senhas, tamanhos, etc.)
  {
    vga_write("Layouts disponiveis:\n");
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
      vga_write("  [");
      char idxbuf[4];
      idxbuf[0] = '0' + (char)i;
      idxbuf[1] = ']';
      idxbuf[2] = ' ';
      idxbuf[3] = '\0';
      vga_write(idxbuf);
      vga_write(keyboard_layout_name(i));
      vga_write(" : ");
      vga_write(keyboard_layout_description(i));
      vga_newline();
    }
    char buf[32];
    while (1) {
      tty_set_prompt("Layout do teclado [0-us]: ");
      tty_set_echo(1);
      tty_set_echo_mask('\0');
      tty_show_prompt();
      size_t l = tty_readline(buf, sizeof(buf));
      if (l == 0) {
        keyboard_set_layout_by_name("us");
        break;
      }
      if (l == 1 && buf[0] >= '0' &&
          buf[0] < '0' + (char)keyboard_layout_count()) {
        size_t pick = (size_t)(buf[0] - '0');
        keyboard_set_layout_by_name(keyboard_layout_name(pick));
        vga_write("Layout aplicado.\n");
        break;
      }
      if (keyboard_set_layout_by_name(buf) == 0) {
        vga_write("Layout aplicado.\n");
        break;
      }
      vga_write("Layout desconhecido. Use um dos listados acima.\n");
    }
  }

  // Detecta discos
  struct dev_choice choices[4];
  size_t ndev = 0;
  extern int ata_devices_count(void);
  extern struct block_device *ata_device_by_index(int);
  int ac = ata_devices_count();
  for (int i = 0; i < ac && ndev < 4; i++) {
    struct block_device *d = ata_device_by_index(i);
    if (d) {
      choices[ndev].dev = d;
      choices[ndev].name = d->name;
      choices[ndev].bytes = (uint64_t)d->block_size * (uint64_t)d->block_count;
      ndev++;
    }
  }
  if (ndev == 0) {
    vga_write("Nenhum dispositivo de bloco encontrado.\n");
    goto hang;
  }
  vga_write("Dispositivos encontrados:\n");
  for (size_t i = 0; i < ndev; i++) {
    char sz[32];
    human_size(choices[i].bytes, sz, sizeof(sz));
    vga_write("  [");
    char ib[2];
    ib[0] = '0' + (char)i;
    ib[1] = '\0';
    vga_write(ib);
    vga_write("] ");
    vga_write(choices[i].name);
    vga_write("  ");
    vga_write(sz);
    vga_write("\n");
  }
  uint32_t pick = 0;
  wizard_prompt_number("Selecionar disco [0]: ", &pick, 0, (uint32_t)(ndev - 1),
                       0);
  struct block_device *target = choices[pick].dev;
  if (!target) {
    goto hang;
  }
  if (target->block_size != 512) {
    vga_write("Dispositivo com block_size !=512.\n");
    goto hang;
  }

  // Particiona (sda1 BOOT, sda2 dados) - SEMPRE cria partiÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Âµes novas
  struct mbr_partition data_part;

  vga_write("Criando tabela de particoes nova (instalacao limpa)...\n");
  uint32_t boot_mb = 32;
  wizard_prompt_number("Tamanho da particao de BOOT (MiB) [32, 16..100]: ",
                       &boot_mb, 16, 100, 32);

  // Use new autonomous bootloader installer
  struct boot_payload_set payloads = boot_embedded_payloads();

  /* DEBUG: Print payload sizes */
  vga_write("[debug] Stage1 size: ");
  {
    char b[32];
    utoa10(payloads.stage1.size, b);
    b[10] = '\0';
    vga_write(b);
    vga_write("\n");
  }
  vga_write("[debug] Stage2 size: ");
  {
    char b[32];
    utoa10(payloads.stage2.size, b);
    b[10] = '\0';
    vga_write(b);
    vga_write("\n");
  }
  vga_write("[debug] Kernel size: ");
  {
    char b[32];
    utoa10(payloads.kernel_main.size, b);
    b[10] = '\0';
    vga_write(b);
    vga_write("\n");
  }

  if (bootwriter_install_fresh(target, boot_mb, 0, &data_part, &payloads) !=
      0) {
    vga_write("[install] Falha critica ao instalar bootloader/particoes.\n");
    goto hang;
  }
  vga_write("[install] Bootloader gravado e particionamento concluido.\n");

  /* Validacao basica da particao de dados retornada */
  if (data_part.sector_count < CAPYFS_DATA_MIN_SECTORS) {
    vga_write("Particao de dados menor que o minimo suportado.\n");
    goto hang;
  }

  struct block_device *part =
      block_offset_wrap(target, data_part.lba_start, data_part.sector_count);
  if (!part) {
    vga_write("Falha ao mapear a particao de dados.\n");
    goto hang;
  }
  struct block_device *chunked = block_chunked_wrap(part, CAPYFS_BLOCK_SIZE);
  struct block_device *dev4096 = chunked ? chunked : part;

  // Criptografia + format/mount
  char pass1[128], pass2[128];
  while (1) {
    vga_write("Defina a senha do volume cifrado CAPYFS.\n");
    tty_set_prompt("Nova senha: ");
    tty_set_echo_mask('*');
    tty_show_prompt();
    size_t l1 = tty_readline(pass1, sizeof(pass1));
    tty_set_echo(1);
    tty_set_echo_mask('\0');
    if (l1 == 0) {
      vga_write("Senha vazia nao permitida.\n");
      continue;
    }
    tty_set_prompt("Confirmar senha: ");
    tty_set_echo_mask('*');
    tty_show_prompt();
    size_t l2 = tty_readline(pass2, sizeof(pass2));
    tty_set_echo(1);
    tty_set_echo_mask('\0');
    if (l2 != l1) {
      vga_write("Senhas nao conferem.\n");
      continue;
    }
    int same = 1;
    for (size_t i = 0; i < l1; i++) {
      if (pass1[i] != pass2[i]) {
        same = 0;
        break;
      }
    }
    if (!same) {
      vga_write("Senhas nao conferem.\n");
      continue;
    }
    break;
  }
  uint8_t key1[32], key2[32];
  crypt_derive_xts_keys(pass1, g_disk_salt, sizeof(g_disk_salt),
                        g_kdf_iterations, key1, key2);
  /* Zera senhas do disco imediatamente apos derivar as chaves. */
  memzero(pass1, sizeof(pass1));
  memzero(pass2, sizeof(pass2));
  struct block_device *crypt_dev = crypt_init(dev4096, key1, key2);
  memzero(key1, sizeof(key1));
  memzero(key2, sizeof(key2));
  if (!crypt_dev || crypt_dev == dev4096) {
    vga_write("Falha ao iniciar camada criptografica (volume inseguro).\n");
    goto hang;
  }
  if (format_and_mount(crypt_dev) != 0)
    goto hang;

  // Wizard de configuracao inicial (pede senha separada para o admin)
  if (system_run_first_boot_setup() != 0) {
    vga_write("Falha no assistente de configuracao inicial.\n");
    goto hang;
  }
  vga_write("\nSelecione o layout final do teclado (sera salvo em "
            "/system/config.ini):\n");
  for (size_t i = 0; i < keyboard_layout_count(); ++i) {
    vga_write("  [");
    char idxbuf[4];
    idxbuf[0] = '0' + (char)i;
    idxbuf[1] = ']';
    idxbuf[2] = ' ';
    idxbuf[3] = '\0';
    vga_write(idxbuf);
    vga_write(keyboard_layout_name(i));
    vga_write(" : ");
    vga_write(keyboard_layout_description(i));
    vga_newline();
  }
  char layout_choice[32];
  const char *current_layout = keyboard_current_layout();
  if (!current_layout)
    current_layout = "us";
  copy_string(layout_choice, sizeof(layout_choice), current_layout);
  vga_write("Layout atual: ");
  vga_write(current_layout);
  vga_newline();
  while (1) {
    tty_set_prompt("Layout final [atual]: ");
    tty_set_echo(1);
    tty_set_echo_mask('\0');
    tty_show_prompt();
    size_t l = tty_readline(layout_choice, sizeof(layout_choice));
    if (l == 0) {
      copy_string(layout_choice, sizeof(layout_choice), current_layout);
    } else if (l == 1 && layout_choice[0] >= '0' &&
               layout_choice[0] < '0' + (char)keyboard_layout_count()) {
      size_t idx = (size_t)(layout_choice[0] - '0');
      copy_string(layout_choice, sizeof(layout_choice),
                  keyboard_layout_name(idx));
    }
    if (keyboard_set_layout_by_name(layout_choice) == 0) {
      vga_write("Layout aplicado para uso e persistencia.\n");
      break;
    }
    vga_write(
        "Layout desconhecido. Escolha pelos indices ou nomes listados.\n");
  }
  if (system_save_keyboard_layout(layout_choice) != 0) {
    vga_write("Aviso: nao foi possivel salvar layout em /system/config.ini.\n");
  }
  // NEW: Save layout to boot config sector (LBA 1) for pre-boot loading
  if (bootwriter_write_config(target, layout_choice) != 0) {
    vga_write("Aviso: falha ao gravar configuracao de boot (layout nao "
              "persistira no boot).\n");
  } else {
    vga_write("Layout de teclado gravado no setor de boot.\n");
  }

  if (system_mark_first_boot_complete() != 0) {
    vga_write("Nao foi possivel registrar conclusao da instalacao.\n");
    goto hang;
  }
  struct super_block *rsb = vfs_root();
  if (rsb && rsb->bdev)
    buffer_cache_sync(rsb->bdev);
  vga_write("Instalacao concluida com sucesso!\n");
  vga_write("Remova a midia de instalacao (ISO) antes de reiniciar.\n");
  wait_and_reboot();
hang:
  vga_write(
      "\nErro durante instalacao. Pressione qualquer tecla para reiniciar.\n");
  wait_and_reboot();
}
