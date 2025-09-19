#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "pit.h"
#include "kmem.h"
#include "buffer.h"
#include "ramdisk.h"
#include "crypt.h"
#include "vfs.h"
#include "noirfs.h"
#include "tty.h"

static struct super_block root_sb;
static const char *g_default_passphrase = "noiros-passphrase";
static const uint8_t g_disk_salt[16] = {
    0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
    0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00
};
static const uint32_t g_kdf_iterations = 16000;
static int format_progress_complete = 0;

static void memzero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

static size_t utoa10(uint32_t value, char *dst) {
    char tmp[10];
    size_t len = 0;
    if (value == 0) {
        dst[0] = '0';
        return 1;
    }
    while (value && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (size_t i = 0; i < len; ++i) {
        dst[i] = tmp[len - 1 - i];
    }
    return len;
}

static void format_progress(const char *stage, uint32_t percent) {
    static uint32_t last_percent = 0xFFFFFFFFu;
    static char last_stage[32] = {0};

    if (!stage) {
        stage = "";
    }
    if (percent > 100) {
        percent = 100;
    }

    // Compare exact stage strings safely (up to last_stage capacity - 1)
    int same_stage = 1;
    for (size_t i = 0; i < sizeof(last_stage) - 1; ++i) {
        char a = last_stage[i];
        char b = stage[i];
        if (a != b) { same_stage = 0; break; }
        if (a == '\0') { break; }
    }
    // If one string ended but the other continues within the limit, they differ
    if (same_stage) {
        size_t i = 0;
        while (i < sizeof(last_stage) - 1 && last_stage[i] && stage[i]) { ++i; }
        if (i < sizeof(last_stage) - 1 && (last_stage[i] != stage[i])) {
            same_stage = 0;
        }
    }

    if (same_stage && percent == last_percent) {
        return;
    }

    char numbuf[10];
    size_t numlen = utoa10(percent, numbuf);
    numbuf[numlen] = '\0';

    vga_write("Formatacao: ");
    vga_write(numbuf);
    vga_write("% ");
    vga_write(stage);
    vga_newline();
    // Pequeno atraso para tornar visível no QEMU mesmo em RAM
    for (volatile uint32_t spin = 0; spin < 200000; ++spin) {
        __asm__ volatile("");
    }

    last_percent = percent;
    size_t copy_len = 0;
    for (; copy_len < sizeof(last_stage) - 1 && stage[copy_len]; ++copy_len) {
        last_stage[copy_len] = stage[copy_len];
    }
    last_stage[copy_len] = '\0';

    if (percent >= 100) {
        format_progress_complete = 1;
        last_percent = 0xFFFFFFFFu;
        last_stage[0] = '\0';
    }
}
void kernel_main(void) {
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

    tty_init();
    keyboard_init();

    vga_write("Digite a senha para montar NoirFS (Enter para usar o padrao).\n");
    tty_set_prompt("Senha: ");
    tty_set_echo_mask('*');
    tty_show_prompt();

    sti();

    size_t pass_len = tty_readline(line, sizeof(line));
    tty_set_echo(1);
    tty_set_echo_mask('\0');

    const char *passphrase = line;
    if (pass_len == 0) {
        passphrase = g_default_passphrase;
        vga_write("Senha vazia: utilizando senha padrao.\n");
    } else {
        vga_write("Senha recebida.\n");
    }

    crypt_derive_xts_keys(passphrase, g_disk_salt, sizeof(g_disk_salt),
                          g_kdf_iterations, key1, key2);
    memzero(line, sizeof(line));

    struct block_device *root_dev = ramdisk_device();
    if (!root_dev) {
        vga_write("RAMDISK indisponivel\n");
    } else {
        struct block_device *crypt_dev = crypt_init(root_dev, key1, key2);
        memzero(key1, sizeof(key1));
        memzero(key2, sizeof(key2));

        if (!crypt_dev) {
            vga_write("Falha ao inicializar camada criptografica\n");
        } else {
            vga_write("Formatando NoirFS...\n");
            format_progress_complete = 0;
            int fmt = noirfs_format(crypt_dev, 128, crypt_dev->block_count, format_progress);
            if (!format_progress_complete) {
                vga_write("\n");
            }

            if (fmt != 0) {
                vga_write("Falha ao formatar NoirFS\n");
            } else if (mount_noirfs(crypt_dev, &root_sb) != 0) {
                vga_write("Falha ao montar NoirFS\n");
            } else if (vfs_mount_root(&root_sb) != 0) {
                vga_write("Falha ao registrar raiz do VFS\n");
            } else {
                vga_write("NoirFS montado em / (dados cifrados)\n");

                if (vfs_create("/hello.txt", VFS_MODE_FILE) == 0) {
                    struct file *hello = vfs_open("/hello.txt", 0);
                    if (hello) {
                        const char msg[] = "NoirFS rodando\n";
                        vfs_write(hello, msg, sizeof(msg) - 1);
                        vfs_close(hello);
                    }
                }

                struct file *reader = vfs_open("/hello.txt", 0);
                if (reader) {
                    char buf[64];
                    long read = vfs_read(reader, buf, sizeof(buf) - 1);
                    if (read > 0) {
                        buf[read] = '\0';
                        vga_write("Conteudo /hello.txt: ");
                        vga_write(buf);
                        vga_write("\n");
                    }
                    vfs_close(reader);
                }

                fs_ready = 1;
            }
        }
    }

    memzero(key1, sizeof(key1));
    memzero(key2, sizeof(key2));

    if (fs_ready) {
        vga_write("Sistema pronto.\n");
    } else {
        vga_write("Sistema inicializado sem NoirFS.\n");
    }

    tty_set_echo(1);
    tty_set_prompt("> ");

    while (1) {
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        tty_show_prompt();
        size_t cmd_len = tty_readline(line, sizeof(line));
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        if (cmd_len > 0) {
            vga_write("Comando recebido: ");
            vga_write(line);
            vga_write("\n");
            memzero(line, cmd_len);
        }
    }
}
