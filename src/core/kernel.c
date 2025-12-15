#include <stddef.h>
#include <stdint.h>
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/isr.h"
#include "drivers/input/keyboard.h"
#include "arch/x86/hw/io.h"
#include "drivers/video/vga.h"
#include "drivers/timer/pit.h"
#include "memory/kmem.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/ramdisk.h"
#include "security/crypt.h"
#include "fs/vfs.h"
#include "fs/noirfs.h"
#include "fs/storage/partition.h"
#include "drivers/console/tty.h"
#include "core/system_init.h"
#include "core/user.h"
#include "shell/shell.h"

static struct super_block root_sb;
static const uint8_t g_disk_salt[16] = {
    0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
    0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00
};
static const uint32_t g_kdf_iterations = 16000;

static void memzero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
}


static int mount_noirfs_root(struct block_device *crypt_dev) {
    if (mount_noirfs(crypt_dev, &root_sb) != 0) {
        return -1;
    }
    if (vfs_mount_root(&root_sb) != 0) {
        return -1;
    }
    vga_write("NoirFS montado em / (dados cifrados)\n");
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

static void kernel_log_dependency_wait(const char *dependency, const char *target) {
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

static int probe_block_zeroed(struct block_device *dev, uint32_t lba) {
    if (lba >= dev->block_count) return 1; // fora do range: não conclui nada
    size_t bs = dev->block_size;
    uint8_t *buf = (uint8_t *)kalloc(bs);
    if (!buf) return 0; // conservador: tratar como não-zero
    int all_zero = 1;
    if (block_device_read(dev, lba, buf) != 0) {
        all_zero = 0; // falha de leitura: considerar não-zero (para evitar falso "em branco")
    } else {
        for (size_t i = 0; i < bs; ++i) { if (buf[i] != 0) { all_zero = 0; break; } }
    }
    kfree(buf);
    return all_zero;
}

static int device_is_blank(struct block_device *dev) {
    if (!dev || dev->block_count == 0 || dev->block_size == 0) return 1;
    // Verifica múltiplos LBAs, incluindo região após o offset de formatação (128)
    uint32_t samples[9];
    size_t n = 0;
    samples[n++] = 0;
    if (dev->block_count > 1) samples[n++] = 1;
    if (dev->block_count > 2) samples[n++] = 2;
    if (dev->block_count > 128) {
        samples[n++] = 127;
        samples[n++] = 128;
        samples[n++] = 129;
    }
    if (dev->block_count > 256) samples[n++] = 256;
    if (dev->block_count > 4) samples[n++] = dev->block_count/2;
    if (dev->block_count > 0) samples[n++] = dev->block_count-1;

    for (size_t i = 0; i < n; ++i) {
        if (!probe_block_zeroed(dev, samples[i])) {
            return 0; // encontrou dado não-zero
        }
    }
    return 1; // todas as amostras zero
}

// Seleciona o backend correto: lê a MBR no disco cru (512B) e expõe a partição 2.
// Em seguida, envolve em blocos de 4096B para alinhar com o NoirFS.
static struct block_device *pick_root_device(struct block_device *raw_ata) {
    if (!raw_ata) {
        return NULL;
    }

    struct block_device *slice = NULL;
    struct mbr_partition data_part;
    if (mbr_read_partition(raw_ata, 1, &data_part) == 0) {
        vga_write("[boot] particao 2 detectada via MBR; usando como volume NoirFS.\n");
        slice = block_offset_wrap(raw_ata, data_part.lba_start, data_part.sector_count);
    } else {
        vga_write("[boot] MBR ou particao 2 indisponivel; usando disco inteiro.\n");
        slice = raw_ata;
    }
    if (!slice) {
        return NULL;
    }
    struct block_device *chunked = block_chunked_wrap(slice, NOIRFS_BLOCK_SIZE);
    if (chunked) {
        return chunked;
    }
    return slice;
}

/* format_and_mount removido: formatação agora é tarefa do instalador (NGIS). */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline; // char* (physical addr)
};

/* Sem modo de instalacao embutido. */

void kernel_main(uint32_t mb_magic, uint32_t mb_info_ptr) {
    (void)mb_magic; (void)mb_info_ptr;
    char line[TTY_BUFFER_MAX];
    uint8_t key1[CRYPT_KEY_SIZE];
    uint8_t key2[CRYPT_KEY_SIZE];
    int fs_ready = 0;

    vga_init();
    vga_write("NoirOS 1 - Versao Singularity esta rodando!\n\n");
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
    extern void ata_init(void);
    extern struct block_device *ata_primary_device(void);
    ata_init();

    tty_init();
    keyboard_init();

    /* Instalacao separada: sem suporte a mode=install no kernel principal. */

    sti();

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
            // Fluxo de volume cifrado existente: obrigatorio autenticar, sem formatar
            vga_write("Volume cifrado detectado. Informe a senha do NoirFS.\n");
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
                    vga_write("Falha ao iniciar camada criptografica (volume inseguro).\n");
                    continue;
                }
                if (mount_noirfs_root(crypt_dev) == 0) {
                    fs_ready = 1;
                } else {
                    vga_write("Senha incorreta. Tente novamente.\n");
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
        kernel_log_process_error(proc_load_initial, "config.ini indisponivel, usando valores padrao.");
        kernel_log_process_finalize(proc_load_initial);
    }

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

                const char *proc_cli = "inicializacao do NoirCLI";
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
        vga_write("Falha ao localizar ou montar volume NoirFS.\n");
        vga_write("Reinicie e inicialize a partir da ISO do instalador para preparar o disco.\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    while (1) {
        __asm__ volatile("hlt");
    }
}
