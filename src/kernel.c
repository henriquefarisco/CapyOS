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
#include "system_init.h"
#include "shell.h"

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

    int same_stage = 1;
    for (size_t i = 0; i < sizeof(last_stage) - 1; ++i) {
        char a = last_stage[i];
        char b = stage[i];
        if (a != b) { same_stage = 0; break; }
        if (a == '\0') { break; }
    }
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

static int format_and_mount(struct block_device *crypt_dev) {
    vga_write("NoirFS indisponivel. Iniciando formatacao...\n");
    format_progress_complete = 0;
    int fmt = noirfs_format(crypt_dev, 128, crypt_dev->block_count, format_progress);
    if (!format_progress_complete) {
        vga_write("\n");
    }
    if (fmt != 0) {
        vga_write("Falha ao formatar NoirFS\n");
        return -1;
    }
    if (mount_noirfs_root(crypt_dev) != 0) {
        vga_write("Falha ao montar NoirFS apos formatacao\n");
        return -1;
    }
    return 0;
}

void kernel_main(void) {
    char line[TTY_BUFFER_MAX];
    uint8_t key1[CRYPT_KEY_SIZE];
    uint8_t key2[CRYPT_KEY_SIZE];
    int fs_ready = 0;
    int formatted_now = 0;
    int first_boot = 0;

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
            if (mount_noirfs_root(crypt_dev) == 0) {
                fs_ready = 1;
            } else if (format_and_mount(crypt_dev) == 0) {
                fs_ready = 1;
                formatted_now = 1;
            }

            if (fs_ready && formatted_now) {
                if (vfs_create("/hello.txt", VFS_MODE_FILE, NULL) == 0) {
                    struct file *hello = vfs_open("/hello.txt", VFS_OPEN_WRITE);
                    if (hello) {
                        const char msg[] = "NoirFS rodando\n";
                        vfs_write(hello, msg, sizeof(msg) - 1);
                        vfs_close(hello);
                    }
                }
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
        const char *proc_first_boot_check = "avaliacao de primeiro boot";
        kernel_log_dependency_wait(proc_session, proc_first_boot_check);
        kernel_log_process_begin(proc_first_boot_check);
        kernel_log_process_begin_success(proc_first_boot_check);
        if (formatted_now) {
            kernel_log_process_progress(proc_first_boot_check);
            first_boot = 1;
            kernel_log_process_conclude(proc_first_boot_check);
            kernel_log_process_finalize(proc_first_boot_check);
            kernel_log_process_finalize_success(proc_first_boot_check);
            vga_write("Formatacao recente detectada: tratando como primeiro boot.\n");
        } else {
            kernel_log_process_progress(proc_first_boot_check);
            first_boot = system_detect_first_boot();
            if (first_boot) {
                vga_write("Primeiro boot identificado.\n");
            } else {
                vga_write("Primeiro boot ja executado anteriormente.\n");
            }
            kernel_log_process_conclude(proc_first_boot_check);
            kernel_log_process_finalize(proc_first_boot_check);
            kernel_log_process_finalize_success(proc_first_boot_check);
        }

        const char *proc_setup = "assistente de configuracao inicial";
        kernel_log_dependency_wait(proc_first_boot_check, proc_setup);
        if (first_boot) {
            kernel_log_process_begin(proc_setup);
            kernel_log_process_begin_success(proc_setup);
            if (system_run_first_boot_setup() != 0) {
                kernel_log_process_error(proc_setup, "falha ao executar assistente.");
                kernel_log_process_finalize(proc_setup);
            } else {
                kernel_log_process_progress(proc_setup);
                kernel_log_process_conclude(proc_setup);
                kernel_log_process_finalize(proc_setup);
                kernel_log_process_finalize_success(proc_setup);
            }
        } else {
            kernel_log_process_begin(proc_setup);
            kernel_log_process_begin_success(proc_setup);
            vga_write("Processo assistente de configuracao inicial dispensado (ja concluido).\n");
            kernel_log_process_progress(proc_setup);
            kernel_log_process_conclude(proc_setup);
            kernel_log_process_finalize(proc_setup);
            kernel_log_process_finalize_success(proc_setup);
        }

        vga_write("Sistema pronto.\n");

        const char *proc_reload_settings = "recarregamento das configuracoes";
        kernel_log_dependency_wait(proc_setup, proc_reload_settings);
        kernel_log_process_begin(proc_reload_settings);
        kernel_log_process_begin_success(proc_reload_settings);
        if (system_load_settings(&settings) == 0) {
            kernel_log_process_progress(proc_reload_settings);
            kernel_log_process_conclude(proc_reload_settings);
            kernel_log_process_finalize(proc_reload_settings);
            kernel_log_process_finalize_success(proc_reload_settings);
        } else {
            kernel_log_process_error(proc_reload_settings, "config.ini indisponivel apos setup.");
            kernel_log_process_finalize(proc_reload_settings);
        }

        const char *proc_theme_after = "aplicacao do tema apos setup";
        kernel_log_dependency_wait(proc_reload_settings, proc_theme_after);
        kernel_log_process_begin(proc_theme_after);
        kernel_log_process_begin_success(proc_theme_after);
        system_apply_theme(&settings);
        kernel_log_process_progress(proc_theme_after);
        kernel_log_process_conclude(proc_theme_after);
        kernel_log_process_finalize(proc_theme_after);
        kernel_log_process_finalize_success(proc_theme_after);

        const char *proc_splash = "exibicao do splash";
        kernel_log_dependency_wait(proc_theme_after, proc_splash);
        kernel_log_process_begin(proc_splash);
        kernel_log_process_begin_success(proc_splash);
        system_show_splash(&settings);
        kernel_log_process_progress(proc_splash);
        kernel_log_process_conclude(proc_splash);
        kernel_log_process_finalize(proc_splash);
        kernel_log_process_finalize_success(proc_splash);

        const char *proc_login = "autenticacao de usuario";
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
        vga_write("Sistema inicializado sem NoirFS.\n");
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

    while (1) {
        __asm__ volatile("hlt");
    }
}
